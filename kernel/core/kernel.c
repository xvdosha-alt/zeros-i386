#include "vga.h"
#include "idt.h"
#include "io.h"
#include "mm.h"
#include "vfs.h"
#include "tty.h"
#include "timer.h"
#include "proc.h"
#include "syscall.h"
#include "initrd.h"
#include "zerosd.h"
#include "net.h"
#include "fat.h"
#include "mouse.h"
#include "display.h"
#include "rtc.h"

extern const uint8_t _initrd_start[];
extern const uint8_t _initrd_end[];

void kernel_main(uint32_t multiboot_info)
{
    tty_init();
    tty_writeln("zerOS boot");
    mm_init(multiboot_info);
    idt_init();
    timer_init(100);
    rtc_init();
    mouse_init();
    vfs_init();
    initrd_unpack(_initrd_start, (uint32_t)(_initrd_end - _initrd_start));
    vfs_mkdir("/sys/tmp");
    vfs_mkdir("/sys/run");
    vfs_mkdir("/sys/home");
    proc_init();
    display_init();
    syscall_init();
    net_init();
    fat_init();
    zerosd_start();
}
