NASM := nasm
CC   := zig cc
LD   := zig ld.lld
HOSTCC := cc
QEMU := qemu-system-i386
PYTHON := python3

OUT := out

KINCS := -I. -Ikernel/core -Ikernel/cpu -Ikernel/drivers -Ikernel/fs -Ikernel/proc -Ikernel/net -Ikernel/gui

CFLAGS := -target x86-freestanding \
	-ffreestanding -fno-stack-protector -fno-pic -fno-pie \
	-nostdlib -mno-sse -mno-sse2 -mno-mmx -msoft-float \
	-fno-omit-frame-pointer \
	-ffunction-sections -fdata-sections \
	-Wall -Wextra -Os $(KINCS)

ASMFLAGS := -f elf32

# Host UTC unix time baked into the guest clock (CMOS is unsafe on QEMU/TCG).
RTC_EPOCH ?= $(shell date -u +%s)

HOSTCFLAGS := -DMP_HOST -Wall -Wextra -Os -ffunction-sections -fdata-sections -Ipy
HOSTLDFLAGS := -Wl,-dead_strip

USER_CFLAGS := -target x86-freestanding \
	-ffreestanding -fno-stack-protector -fno-pic -fno-pie \
	-nostdlib -mno-sse -mno-sse2 -mno-mmx -msoft-float \
	-Wall -Wextra -Os -I. -Iuser/lib $(KINCS)

KERNEL_OBJS := \
	$(OUT)/boot/entry.o \
	$(OUT)/kernel/core/kernel.o \
	$(OUT)/kernel/drivers/vga.o \
	$(OUT)/kernel/drivers/kbd.o \
	$(OUT)/kernel/drivers/fb.o \
	$(OUT)/kernel/drivers/mouse.o \
	$(OUT)/kernel/cpu/idt.o \
	$(OUT)/kernel/core/string.o \
	$(OUT)/kernel/core/mm.o \
	$(OUT)/kernel/fs/vfs.o \
	$(OUT)/kernel/drivers/tty.o \
	$(OUT)/kernel/drivers/timer.o \
	$(OUT)/kernel/drivers/rtc.o \
	$(OUT)/kernel/drivers/pcspeaker.o \
	$(OUT)/kernel/drivers/pci.o \
	$(OUT)/kernel/proc/elf.o \
	$(OUT)/kernel/proc/proc.o \
	$(OUT)/kernel/proc/syscall.o \
	$(OUT)/kernel/fs/initrd.o \
	$(OUT)/kernel/proc/zerosd.o \
	$(OUT)/kernel/net/net.o \
	$(OUT)/kernel/drivers/ata.o \
	$(OUT)/kernel/fs/fat.o \
	$(OUT)/kernel/gui/display.o \
	$(OUT)/kernel/fs/initrd_blob.o

USER_PROGS := msh cat nano mvim pkg ping dns httpd python \
	curl wget nc ssh sshd ifconfig telnet cp rm mv \
	ls tree touch mkdir head tail which uname echo clear pwd \
	true false desktop run limited ramfsinfo date \
	ps kill \
	session wm gterm files netinfo about notepad clock dvd w_test launchpad \
	calc hex paint settings

USER_SRC_msh := user/shell/msh.c
USER_SRC_cat := user/util/cat.c
USER_SRC_cp := user/util/cp.c
USER_SRC_rm := user/util/rm.c
USER_SRC_mv := user/util/mv.c
USER_SRC_ls := user/util/ls.c
USER_SRC_tree := user/util/tree.c
USER_SRC_touch := user/util/touch.c
USER_SRC_mkdir := user/util/mkdir.c
USER_SRC_which := user/util/which.c
USER_SRC_uname := user/util/uname.c
USER_SRC_date := user/util/date.c
USER_SRC_ps := user/util/ps.c
USER_SRC_kill := user/util/kill.c
USER_SRC_echo := user/util/echo.c
USER_SRC_clear := user/util/clear.c
USER_SRC_pwd := user/util/pwd.c
USER_SRC_true := user/util/true.c
USER_SRC_false := user/util/false.c
USER_SRC_desktop := user/util/desktop.c
USER_SRC_run := user/util/run.c
USER_SRC_limited := user/util/limited.c
USER_SRC_ramfsinfo := user/util/ramfsinfo.c
USER_SRC_nano := user/editors/nano.c
USER_SRC_mvim := user/editors/mvim.c
USER_SRC_pkg := user/util/pkg.c
USER_SRC_ping := user/net/ping.c
USER_SRC_dns := user/net/dns.c
USER_SRC_httpd := user/net/httpd.c
USER_SRC_curl := user/net/curl.c
USER_SRC_wget := user/net/wget.c
USER_SRC_nc := user/net/nc.c
USER_SRC_ssh := user/net/ssh.c
USER_SRC_sshd := user/net/sshd.c
USER_SRC_ifconfig := user/net/ifconfig.c
USER_SRC_telnet := user/net/telnet.c
USER_SRC_session := user/util/session.c
USER_SRC_gterm := user/gui/gterm.c
USER_SRC_files := user/gui/files.c
USER_SRC_netinfo := user/gui/netinfo.c
USER_SRC_about := user/gui/about.c
USER_SRC_notepad := user/gui/notepad.c
USER_SRC_clock := user/gui/clock.c
USER_SRC_dvd := user/gui/dvd.c
USER_SRC_w_test := user/gui/w_test.c
USER_SRC_launchpad := user/gui/launchpad.c
USER_SRC_calc := user/gui/calc.c
USER_SRC_hex := user/gui/hex.c
USER_SRC_paint := user/gui/paint.c
USER_SRC_settings := user/gui/settings.c

GUI_SIMPLE := gterm files netinfo about notepad clock dvd w_test launchpad \
	calc hex paint settings
GUI_PROGS := wm $(GUI_SIMPLE)

GRUB_DIR := $(OUT)/iso/boot/grub
GRUB_MKRESCUE ?= i686-elf-grub-mkrescue
ifeq ($(shell test -x /opt/homebrew/bin/i686-elf-grub-mkrescue && echo yes),yes)
GRUB_MKRESCUE := /opt/homebrew/bin/i686-elf-grub-mkrescue
endif

all: userland initrd
	$(MAKE) $(OUT)/kernel.elf $(OUT)/grub.iso $(OUT)/fat.img size

.PHONY: all clean run grub-tree grub.iso host test size userland initrd fat.img fat-reset

$(OUT)/boot/%.o: boot/%.asm
	@mkdir -p $(dir $@)
	$(NASM) $(ASMFLAGS) $< -o $@

$(OUT)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Always pass a fresh host epoch when this object is built.
$(OUT)/kernel/drivers/rtc.o: kernel/drivers/rtc.c kernel/drivers/rtc.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DRTC_SEED_EPOCH=$(RTC_EPOCH)ull -c kernel/drivers/rtc.c -o $@

$(OUT)/kernel/fs/initrd_blob.o: kernel/fs/initrd_blob.asm $(OUT)/initrd.bin
	@mkdir -p $(dir $@)
	$(NASM) $(ASMFLAGS) -I$(OUT)/ kernel/fs/initrd_blob.asm -o $@

$(OUT)/user/%.ld:
	@mkdir -p $(OUT)/user
	@echo 'ENTRY(_start)' > $@
	@echo 'SECTIONS {' >> $@
	@echo '  . = $(BASE);' >> $@
	@echo '  .text : { *(.text*) }' >> $@
	@echo '  .rodata : { *(.rodata*) }' >> $@
	@echo '  .data : { *(.data*) }' >> $@
	@echo '  . = ALIGN(16);' >> $@
	@echo '  .bss (NOLOAD) : {' >> $@
	@echo '    *(.bss*) *(COMMON)' >> $@
	@echo '    . = ALIGN(16);' >> $@
	@echo '  }' >> $@
	@echo '}' >> $@

$(OUT)/user/crt0.o: user/lib/crt0.asm
	@mkdir -p $(OUT)/user
	$(NASM) $(ASMFLAGS) $< -o $@

$(OUT)/user/libmp.o: user/lib/libmp.c user/lib/libmp.h
	@mkdir -p $(OUT)/user
	$(CC) $(USER_CFLAGS) -c user/lib/libmp.c -o $@

define USER_RULE
$(OUT)/user/$(1).elf: $(OUT)/user/crt0.o $(OUT)/user/libmp.o $$(USER_SRC_$(1)) $(OUT)/user/$(1).ld
	$$(CC) $$(USER_CFLAGS) -c $$(USER_SRC_$(1)) -o $(OUT)/user/$(1).o
	$$(LD) -m elf_i386 -T $(OUT)/user/$(1).ld --gc-sections -o $$@ \
		$(OUT)/user/crt0.o $(OUT)/user/libmp.o $(OUT)/user/$(1).o
endef

$(OUT)/user/msh.ld: BASE=0x02000000
$(OUT)/user/cat.ld: BASE=0x02100000
$(OUT)/user/nano.ld: BASE=0x02200000
$(OUT)/user/mvim.ld: BASE=0x02300000
$(OUT)/user/pkg.ld: BASE=0x02400000
$(OUT)/user/ping.ld: BASE=0x02500000
$(OUT)/user/dns.ld: BASE=0x02600000
$(OUT)/user/httpd.ld: BASE=0x02700000
$(OUT)/user/python.ld: BASE=0x02800000
$(OUT)/user/cp.ld: BASE=0x02900000
$(OUT)/user/rm.ld: BASE=0x02a00000
$(OUT)/user/mv.ld: BASE=0x02b00000
$(OUT)/user/curl.ld: BASE=0x02c00000
$(OUT)/user/wget.ld: BASE=0x02d00000
$(OUT)/user/nc.ld: BASE=0x02e00000
$(OUT)/user/ssh.ld: BASE=0x02f00000
$(OUT)/user/sshd.ld: BASE=0x03000000
$(OUT)/user/ifconfig.ld: BASE=0x03100000
$(OUT)/user/telnet.ld: BASE=0x03200000
$(OUT)/user/session.ld: BASE=0x03300000
$(OUT)/user/wm.ld: BASE=0x03400000
$(OUT)/user/gterm.ld: BASE=0x04900000
$(OUT)/user/files.ld: BASE=0x03600000
$(OUT)/user/netinfo.ld: BASE=0x03700000
$(OUT)/user/about.ld: BASE=0x03800000
$(OUT)/user/notepad.ld: BASE=0x04b00000
$(OUT)/user/clock.ld: BASE=0x03500000
$(OUT)/user/dvd.ld: BASE=0x04f00000
$(OUT)/user/w_test.ld: BASE=0x05000000
$(OUT)/user/launchpad.ld: BASE=0x05100000
$(OUT)/user/calc.ld: BASE=0x05200000
$(OUT)/user/hex.ld: BASE=0x05300000
$(OUT)/user/paint.ld: BASE=0x05400000
$(OUT)/user/settings.ld: BASE=0x05500000
$(OUT)/user/ls.ld: BASE=0x03900000
$(OUT)/user/tree.ld: BASE=0x03a00000
$(OUT)/user/touch.ld: BASE=0x03b00000
$(OUT)/user/mkdir.ld: BASE=0x03c00000
$(OUT)/user/head.ld: BASE=0x03d00000
$(OUT)/user/tail.ld: BASE=0x03e00000
$(OUT)/user/which.ld: BASE=0x03f00000
$(OUT)/user/uname.ld: BASE=0x04000000
$(OUT)/user/date.ld: BASE=0x04c00000
$(OUT)/user/ps.ld: BASE=0x04d00000
$(OUT)/user/kill.ld: BASE=0x04e00000
$(OUT)/user/echo.ld: BASE=0x04100000
$(OUT)/user/clear.ld: BASE=0x04200000
$(OUT)/user/pwd.ld: BASE=0x04300000
$(OUT)/user/true.ld: BASE=0x04400000
$(OUT)/user/false.ld: BASE=0x04500000
$(OUT)/user/desktop.ld: BASE=0x04600000
$(OUT)/user/run.ld: BASE=0x04a00000
$(OUT)/user/limited.ld: BASE=0x04700000
$(OUT)/user/ramfsinfo.ld: BASE=0x04800000

$(foreach p,$(filter-out python wm $(GUI_SIMPLE) head tail,$(USER_PROGS)),$(eval $(call USER_RULE,$(p))))

$(OUT)/user/head.elf: $(OUT)/user/crt0.o $(OUT)/user/libmp.o user/util/headtail.c $(OUT)/user/head.ld
	$(CC) $(USER_CFLAGS) -c user/util/headtail.c -o $(OUT)/user/head.o
	$(LD) -m elf_i386 -T $(OUT)/user/head.ld --gc-sections -o $@ \
		$(OUT)/user/crt0.o $(OUT)/user/libmp.o $(OUT)/user/head.o

$(OUT)/user/tail.elf: $(OUT)/user/crt0.o $(OUT)/user/libmp.o user/util/headtail.c $(OUT)/user/tail.ld
	$(CC) $(USER_CFLAGS) -DIS_TAIL -c user/util/headtail.c -o $(OUT)/user/tail.o
	$(LD) -m elf_i386 -T $(OUT)/user/tail.ld --gc-sections -o $@ \
		$(OUT)/user/crt0.o $(OUT)/user/libmp.o $(OUT)/user/tail.o

$(OUT)/user/libgui.o: user/gui/libgui.c user/gui/libgui.h user/lib/libmp.h
	@mkdir -p $(OUT)/user
	$(CC) $(USER_CFLAGS) -Iuser/gui -c user/gui/libgui.c -o $@

GUIC_OBJS := $(OUT)/user/widget.o $(OUT)/user/wlabel.o $(OUT)/user/wbutton.o \
	$(OUT)/user/wwindow.o $(OUT)/user/wscroll.o $(OUT)/user/wentry.o \
	$(OUT)/user/wlist.o $(OUT)/user/wmenu.o $(OUT)/user/wfiledlg.o

$(OUT)/user/widget.o: user/guic/widget.c user/guic/widget.h user/guic/wwindow.h user/gui/libgui.h user/lib/libmp.h
	@mkdir -p $(OUT)/user
	$(CC) $(USER_CFLAGS) -Iuser/guic -Iuser/gui -c user/guic/widget.c -o $@

$(OUT)/user/wlabel.o: user/guic/wlabel.c user/guic/wlabel.h user/guic/widget.h user/gui/libgui.h
	@mkdir -p $(OUT)/user
	$(CC) $(USER_CFLAGS) -Iuser/guic -Iuser/gui -c user/guic/wlabel.c -o $@

$(OUT)/user/wbutton.o: user/guic/wbutton.c user/guic/wbutton.h user/guic/widget.h user/guic/wwindow.h user/gui/libgui.h
	@mkdir -p $(OUT)/user
	$(CC) $(USER_CFLAGS) -Iuser/guic -Iuser/gui -c user/guic/wbutton.c -o $@

$(OUT)/user/wwindow.o: user/guic/wwindow.c user/guic/wwindow.h user/guic/widget.h user/gui/libgui.h
	@mkdir -p $(OUT)/user
	$(CC) $(USER_CFLAGS) -Iuser/guic -Iuser/gui -c user/guic/wwindow.c -o $@

$(OUT)/user/wscroll.o: user/guic/wscroll.c user/guic/wscroll.h user/guic/widget.h user/gui/libgui.h
	@mkdir -p $(OUT)/user
	$(CC) $(USER_CFLAGS) -Iuser/guic -Iuser/gui -c user/guic/wscroll.c -o $@

$(OUT)/user/wentry.o: user/guic/wentry.c user/guic/wentry.h user/guic/widget.h user/gui/libgui.h
	@mkdir -p $(OUT)/user
	$(CC) $(USER_CFLAGS) -Iuser/guic -Iuser/gui -c user/guic/wentry.c -o $@

$(OUT)/user/wlist.o: user/guic/wlist.c user/guic/wlist.h user/guic/widget.h user/gui/libgui.h
	@mkdir -p $(OUT)/user
	$(CC) $(USER_CFLAGS) -Iuser/guic -Iuser/gui -c user/guic/wlist.c -o $@

$(OUT)/user/wmenu.o: user/guic/wmenu.c user/guic/wmenu.h user/guic/widget.h user/gui/libgui.h
	@mkdir -p $(OUT)/user
	$(CC) $(USER_CFLAGS) -Iuser/guic -Iuser/gui -c user/guic/wmenu.c -o $@

$(OUT)/user/wfiledlg.o: user/guic/wfiledlg.c user/guic/wfiledlg.h user/guic/guic.h user/gui/libgui.h
	@mkdir -p $(OUT)/user
	$(CC) $(USER_CFLAGS) -Iuser/guic -Iuser/gui -c user/guic/wfiledlg.c -o $@

$(OUT)/user/termrun.o: user/gui/termrun.c user/gui/termrun.h user/lib/libmp.h
	@mkdir -p $(OUT)/user
	$(CC) $(USER_CFLAGS) -Iuser/gui -c user/gui/termrun.c -o $@

$(OUT)/user/wm.elf: $(OUT)/user/crt0.o $(OUT)/user/libmp.o $(OUT)/user/libgui.o $(GUIC_OBJS) $(OUT)/user/termrun.o user/gui/wm.c $(OUT)/user/wm.ld
	$(CC) $(USER_CFLAGS) -Iuser/gui -c user/gui/wm.c -o $(OUT)/user/wm.o
	$(LD) -m elf_i386 -T $(OUT)/user/wm.ld --gc-sections -o $@ \
		$(OUT)/user/crt0.o $(OUT)/user/libmp.o $(OUT)/user/libgui.o $(GUIC_OBJS) $(OUT)/user/termrun.o $(OUT)/user/wm.o

define GUI_APP_RULE
$(OUT)/user/$(1).elf: $(OUT)/user/crt0.o $(OUT)/user/libmp.o $(OUT)/user/libgui.o $(GUIC_OBJS) $$(USER_SRC_$(1)) $(OUT)/user/$(1).ld
	$$(CC) $$(USER_CFLAGS) -Iuser/gui -c $$(USER_SRC_$(1)) -o $(OUT)/user/$(1).o
	$$(LD) -m elf_i386 -T $(OUT)/user/$(1).ld --gc-sections -o $$@ \
		$(OUT)/user/crt0.o $(OUT)/user/libmp.o $(OUT)/user/libgui.o $(GUIC_OBJS) $(OUT)/user/$(1).o
endef
$(foreach p,$(filter-out gterm,$(GUI_SIMPLE)),$(eval $(call GUI_APP_RULE,$(p))))

$(OUT)/user/gterm.elf: $(OUT)/user/crt0.o $(OUT)/user/libmp.o $(OUT)/user/libgui.o $(GUIC_OBJS) $(OUT)/user/termrun.o user/gui/gterm.c $(OUT)/user/gterm.ld
	$(CC) $(USER_CFLAGS) -Iuser/gui -c user/gui/gterm.c -o $(OUT)/user/gterm.o
	$(LD) -m elf_i386 -T $(OUT)/user/gterm.ld --gc-sections -o $@ \
		$(OUT)/user/crt0.o $(OUT)/user/libmp.o $(OUT)/user/libgui.o $(GUIC_OBJS) $(OUT)/user/termrun.o $(OUT)/user/gterm.o

PY_USER_SRCS := py/util.c py/heap.c py/object.c py/lex.c py/parse.c py/eval.c \
	py/runtime.c py/repl.c py/fs.c py/seed.c user/python/host_io_user.c user/python/python_main.c

$(OUT)/user/python.elf: $(OUT)/user/crt0.o $(OUT)/user/libmp.o $(OUT)/user/python.ld $(PY_USER_SRCS)
	@mkdir -p $(OUT)/user
	$(CC) $(USER_CFLAGS) -c user/python/python_main.c -o $(OUT)/user/python_main.o
	$(CC) $(USER_CFLAGS) -c user/python/host_io_user.c -o $(OUT)/user/host_io_user.o
	$(CC) $(USER_CFLAGS) -c py/util.c -o $(OUT)/user/py_util.o
	$(CC) $(USER_CFLAGS) -c py/heap.c -o $(OUT)/user/py_heap.o
	$(CC) $(USER_CFLAGS) -c py/object.c -o $(OUT)/user/py_object.o
	$(CC) $(USER_CFLAGS) -c py/lex.c -o $(OUT)/user/py_lex.o
	$(CC) $(USER_CFLAGS) -c py/parse.c -o $(OUT)/user/py_parse.o
	$(CC) $(USER_CFLAGS) -c py/eval.c -o $(OUT)/user/py_eval.o
	$(CC) $(USER_CFLAGS) -c py/runtime.c -o $(OUT)/user/py_runtime.o
	$(CC) $(USER_CFLAGS) -c py/repl.c -o $(OUT)/user/py_repl.o
	$(CC) $(USER_CFLAGS) -c py/fs.c -o $(OUT)/user/py_fs.o
	$(CC) $(USER_CFLAGS) -c py/seed.c -o $(OUT)/user/py_seed.o
	$(LD) -m elf_i386 -T $(OUT)/user/python.ld --gc-sections -o $@ \
		$(OUT)/user/crt0.o $(OUT)/user/libmp.o $(OUT)/user/python_main.o $(OUT)/user/host_io_user.o \
		$(OUT)/user/py_util.o $(OUT)/user/py_heap.o $(OUT)/user/py_object.o $(OUT)/user/py_lex.o \
		$(OUT)/user/py_parse.o $(OUT)/user/py_eval.o $(OUT)/user/py_runtime.o $(OUT)/user/py_repl.o \
		$(OUT)/user/py_fs.o $(OUT)/user/py_seed.o

userland: $(foreach p,$(USER_PROGS),$(OUT)/user/$(p).elf)
	@mkdir -p initrd/bin
	@rm -rf initrd/gui
	@mkdir -p initrd/gui
	@for p in $(filter-out $(GUI_PROGS) limited ramfsinfo,$(USER_PROGS)); do \
		cp $(OUT)/user/$$p.elf initrd/bin/$$p; \
	done
	@cp $(OUT)/user/wm.elf initrd/gui/wm
	@for p in $(GUI_SIMPLE); do cp $(OUT)/user/$$p.elf initrd/gui/$$p.win; done
	@rm -f $(foreach p,$(GUI_PROGS),initrd/bin/$(p))
	@cp $(OUT)/user/clear.elf initrd/bin/cls
	@for a in df du free; do cp $(OUT)/user/ramfsinfo.elf initrd/bin/$$a; done
	@for a in jobs fg bg pkill top htop alias; do \
		cp $(OUT)/user/limited.elf initrd/bin/$$a; \
	done
	@rm -f initrd/bin/pslist initrd/bin/pskill

initrd: userland
	@mkdir -p $(OUT)
	$(PYTHON) scripts/pack_initrd.py initrd $(OUT)/initrd.bin

$(OUT)/kernel.elf: $(KERNEL_OBJS) boot/linker.ld
	$(LD) -m elf_i386 -T boot/linker.ld --gc-sections -o $@ $(KERNEL_OBJS)

$(GRUB_DIR)/grub.cfg: boot/grub.cfg
	mkdir -p $(GRUB_DIR)
	cp $< $@

$(OUT)/iso/boot/kernel.elf: $(OUT)/kernel.elf
	mkdir -p $(OUT)/iso/boot
	cp $< $@

grub-tree: $(OUT)/iso/boot/kernel.elf $(GRUB_DIR)/grub.cfg

$(OUT)/fat.img:
	@mkdir -p $(OUT) disk/seed
	$(PYTHON) scripts/mkfat.py $@

fat.img: $(OUT)/fat.img

fat-reset:
	@mkdir -p $(OUT) disk/seed
	$(PYTHON) scripts/mkfat.py $(OUT)/fat.img
	@echo "FAT disk reset ($(OUT)/fat.img)"

$(OUT)/grub.iso: grub-tree
	$(GRUB_MKRESCUE) -o $@ $(OUT)/iso

grub.iso: $(OUT)/grub.iso

run: $(OUT)/fat.img
	@# Refresh wall-clock seed from the host, then boot.
	@rm -f $(OUT)/kernel/drivers/rtc.o
	@$(MAKE) --no-print-directory $(OUT)/grub.iso RTC_EPOCH=$$(date -u +%s)
	@stty -ixon 2>/dev/null || true
	$(QEMU) -boot d -cdrom $(OUT)/grub.iso -serial stdio -vga std \
		-rtc base=utc,clock=host \
		-drive file=$(OUT)/fat.img,format=raw,if=ide,index=0,media=disk \
		-netdev user,id=n0,hostfwd=tcp::8080-:8080,hostfwd=tcp::2222-:22 \
		-device virtio-net-pci,netdev=n0

HOST_PY := py/util.c py/heap.c py/object.c py/lex.c py/parse.c py/eval.c \
	py/runtime.c py/repl.c py/fs.c py/seed.c py/host_io_stdio.c

host: $(HOST_PY) host/host_main.c
	$(HOSTCC) $(HOSTCFLAGS) $(HOSTLDFLAGS) -o host/minipy $(HOST_PY) host/host_main.c

TEST_PY := py/util.c py/heap.c py/object.c py/lex.c py/parse.c py/eval.c \
	py/runtime.c py/fs.c py/seed.c py/host_io_stdio.c

test: $(TEST_PY) host/test_runner.c tests/cases.txt
	$(HOSTCC) $(HOSTCFLAGS) $(HOSTLDFLAGS) -o host/test_runner $(TEST_PY) host/test_runner.c
	./host/test_runner tests/cases.txt

size: $(OUT)/kernel.elf
	@echo "=== zerOS footprint ==="
	@python3 -c "import os; n=os.path.getsize('$(OUT)/kernel.elf'); print('kernel.elf: %d bytes (%.1f KiB)' % (n, n/1024.0))"

clean:
	rm -rf $(OUT) build \
		boot/*.o kernel/core/*.o kernel/cpu/*.o kernel/drivers/*.o \
		kernel/fs/*.o kernel/proc/*.o kernel/net/*.o py/*.o \
		host/minipy host/test_runner \
		kernel.elf grub.iso \
		initrd/bin/* initrd/gui/*
