[EN](../README.md) | RU

# zerOS

![C](https://img.shields.io/badge/C-A8B9CC?style=flat-square&logo=c&logoColor=black)


Bare-metal **i386** micro-OS: Multiboot kernel, cooperative processes, in-memory ramfs, virtio networking и GUI desktop в духе Win95 - полностью freestanding, без libc.

![zerOS desktop](demo.jpg)

| | |
|---|---|
| Arch | IA-32, Multiboot, flat memory (без MMU) |
| Isolation | rings 0/3, identity map |
| Init | `zerosd` (unit-file PID 1) |
| FS | ramfs; initrd blob в kernel `.rodata` |
| Net | virtio-net + custom IPv4/TCP/UDP/ICMP |
| Userland | ET_EXEC ELF (Zig freestanding) |
| Display | VGA text **или** VESA framebuffer GUI |
| Footprint | `kernel.elf` ~ несколько сотен KiB с initrd |

```
GRUB ──► kernel.elf ──► unpack initrd ──► zerosd
                                              │
                    local-fs ──► netd ──► msh / desktop session
```

---

## Быстрый старт

**Нужно:** `nasm`, `zig`, `python3`, `qemu-system-i386`, `xorriso` (для ISO).

```bash
make all          # kernel + userland + initrd + image
make run          # QEMU (serial на stdio; GUI если session стартует)
make test         # host minipy regression suite
```

Консольная телеметрия: COM1 (`-serial stdio`) и Bochs port `0xE9`.

---

## Desktop (GUI)

Загрузка в framebuffer mode; `session` запускает window manager.

| Компонент | Path | Роль |
|---|---|---|
| `wm` | `/sys/gui/wm.win` | Window manager, wallpaper, icons, taskbar |
| `launchpad` | `/sys/gui/launchpad.win` | Start-menu popup (`[Z]` на taskbar) |
| `gterm` | | Terminal (Ctrl+C убивает child) |
| `files` / `notepad` | | Browser + editor (Open dialogs, clipboard) |
| `calc` / `hex` / `paint` / `settings` | | Дополнительные desktop apps |

**Desktop icons** берутся из `/sys/etc/gui_desktop.txt` (double-click для запуска). **Alt+Tab** переключает окна. Edges/corners меняют размер hosted apps; drag maximized window восстанавливает и перемещает. PC speaker beeps при session start и некоторых UI actions (`SYS_BEEP`).

**Taskbar:** `[Z]` открывает launchpad (слева); `X` выходит из session (справа).

**Clipboard:** `SYS_CLIP_SET` / `SYS_CLIP_GET` - Notepad Ctrl+C/V, Files Ctrl+C копирует path.

**Network:** `/sys/etc/network.conf` (`ip=`/`mask=`/`gw=`/`dns=`) или QEMU SLIRP defaults.

**Shell pipes:** `msh` поддерживает `cmd | cmd` через `SYS_PIPE`.

---

## Boot & architecture

1. GRUB загружает `/boot/kernel.elf` (Multiboot `0x1BADB002`, load at 1 MiB).
2. `entry.asm` устанавливает flat GDT (kernel/user code+data + TSS) и переходит в `kernel_main`.
3. Порядок init:

```
tty → mm → IDT → PIT@100Hz → vfs → initrd → proc → syscalls → net → zerosd
```

Initrd **не** Multiboot module: `scripts/pack_initrd.py` упаковывает `initrd/` в TLV blob, который `INCBIN`'ится в kernel.

**Без paging.** Physical == virtual. ELF loader пишет `PT_LOAD` в фиксированный VMA на программу. Scheduling cooperative / blocking (`spawn` → `wait`); timer tick не preempt'ит user code.

---

## Shell & network tools

`msh` - интерактивная shell. Полезные builtins / bins:

```text
ls  tree  cat  cp  rm  mv  mkdir  touch
ping  dns  ifconfig  curl  wget  nc  telnet
httpd  sshd  ssh          # cleartext lab tools, не OpenSSH/TLS
python                    # guest minipy
desktop / session         # войти в GUI
```

Со стороны host, пока guest servers работают (SLIRP):

```bash
curl -s http://127.0.0.1:8080/
nc 127.0.0.1 2222
```

---

## minipy

Один language core, два envelope (host `MP_HOST` и guest через syscalls): ints/bools/strings/lists, `def`/`if`/`while`/`for`/`import`, небольшой VFS.

```bash
make host && ./host/minipy
make test                 # ./host/test_runner tests/cases.txt
```

---

## Tree

```
typewriter/
├── boot/ entry.asm, linker.ld, grub.cfg
├── kernel/          mm vfs proc elf syscall zerosd pci net gui
├── user/
│   ├── lib/         libmp (syscalls + GUI ABI)
│   ├── guic/        widget toolkit
│   ├── gui/         wm, launchpad, gterm, files, …
│   ├── shell/       msh
│   └── util/ net/   classic userland
├── py/              minipy
├── initrd/          seed tree (etc/, gui_launch.txt, …)
├── host/            host minipy + test_runner
├── scripts/         pack_initrd.py
└── Makefile
```

Build artefacts в `out/` (gitignored). Packed binaries в `initrd/bin` и `initrd/gui` пересоздаются через `make`.

---

## Non-goals

Без glibc, paging/ASLR, preemptive SMP, pipes/job control, DAC, TLS и real DHCP. `curl`/`ssh` здесь - cleartext lab clients поверх in-tree stack.

---

## Status

ABI и GUI toolkit ещё меняются. Лучшие entry points: `boot/entry.asm` → `kernel_main` → `zerosd` → `msh` / `wm`.
