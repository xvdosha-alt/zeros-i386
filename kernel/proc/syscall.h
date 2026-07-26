#ifndef KERNEL_SYSCALL_H
#define KERNEL_SYSCALL_H

#include "types.h"

enum {
    SYS_EXIT = 1,
    SYS_WRITE = 2,
    SYS_READ = 3,
    SYS_OPEN = 4,
    SYS_CLOSE = 5,
    SYS_SPAWN = 6,
    SYS_WAIT = 7,
    SYS_YIELD = 8,
    SYS_LISTDIR = 9,
    SYS_UNLINK = 10,
    SYS_MKDIR = 11,
    SYS_EXISTS = 12,
    SYS_READFILE = 13,
    SYS_WRITEFILE = 14,
    SYS_GETPID = 15,
    SYS_CHDIR = 16,
    SYS_GETCWD = 17,
    SYS_IOCTL = 18,
    SYS_SOCKET = 19,
    SYS_BIND = 20,
    SYS_LISTEN = 21,
    SYS_ACCEPT = 22,
    SYS_CONNECT = 23,
    SYS_SEND = 24,
    SYS_RECV = 25,
    SYS_DNS = 26,
    SYS_IFCONFIG = 27,
    SYS_PING = 28,
    SYS_FB_INFO = 29,
    SYS_FB_MODE = 30,
    SYS_INPUT_POLL = 31,
    SYS_INPUT_FIELD = 32,
    SYS_FB_FLIP = 33,
    SYS_CONS_READ = 34,
    SYS_CONS_WRITE = 35,
    SYS_GUI_SERVER = 36,
    SYS_GUI_CREATE = 37,
    SYS_GUI_FB = 38,
    SYS_GUI_DAMAGE = 39,
    SYS_GUI_DESTROY = 40,
    SYS_GUI_INFO = 41,
    SYS_GUI_NEXT = 42,
    SYS_GUI_ACK = 43,
    SYS_GUI_LAUNCH = 44,
    SYS_GUI_TAKE_LAUNCH = 45,
    SYS_GUI_TITLE = 46,
    SYS_GUI_POST = 47,
    SYS_GUI_POLL = 48,
    SYS_KILL = 49,
    SYS_GUI_FIND = 50,
    SYS_TIME = 51,
    SYS_PSINFO = 52,
    SYS_GUI_RESIZE = 53,
    SYS_GUI_FLAGS = 54,
    SYS_BEEP = 55,
    SYS_CLIP_SET = 56,
    SYS_CLIP_GET = 57,
    SYS_PIPE = 58,
    SYS_DUP2 = 59
};

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    uint32_t addr;
} SysFbInfo;

typedef struct {
    uint32_t type;
    int32_t key;
    int32_t x;
    int32_t y;
    int32_t buttons;
    int32_t dx;
    int32_t dy;
} SysInputEvent;

enum {
    INP_NONE = 0,
    INP_KEY = 1,
    INP_MOUSE = 2,
    INP_RESIZE = 3,
    INP_CLOSE = 4
};

void syscall_init(void);
uint32_t syscall_handler(uint32_t *frame);
void sched_loop(void);

#endif
