#ifndef USER_LIBMP_H
#define USER_LIBMP_H

typedef unsigned int uint32_t;
typedef unsigned short uint16_t;
typedef unsigned char uint8_t;
typedef uint32_t size_t;

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
    int key;
    int x;
    int y;
    int buttons;
    int dx;
    int dy;
} SysInputEvent;

typedef struct {
    int year;
    int month;
    int day;
    int hour;
    int min;
    int sec;
} SysTime;

typedef struct {
    int pid;
    int ppid;
    int state; /* 1=ready 2=running 3=blocked 4=zombie */
    char name[32];
} SysProcInfo;

enum {
    INP_NONE = 0,
    INP_KEY = 1,
    INP_MOUSE = 2,
    INP_RESIZE = 3,
    INP_CLOSE = 4
};

#define MOUSE_BTN_LEFT   0x01
#define MOUSE_BTN_RIGHT  0x02
#define MOUSE_BTN_MID    0x04
#define MOUSE_WHEEL_UP   0x10
#define MOUSE_WHEEL_DOWN 0x20
#define MOUSE_BTN_MASK   0x3F

/* Match kernel/drivers/kbd.h */
enum {
    KEY_F4 = 0x107
};
#define KEY_META 0x200
#define KEY_ALT_F4 (KEY_META | KEY_F4)
#define KEY_ALT_TAB (KEY_META | '\t')

static inline int syscall3(int n, int a, int b, int c)
{
    int ret;
    __asm__ volatile (
        "pushl %%ebx\n\t"
        "movl %2, %%ebx\n\t"
        "int $0x80\n\t"
        "popl %%ebx"
        : "=a"(ret)
        : "0"(n), "rm"(a), "c"(b), "d"(c)
        : "memory", "cc"
    );
    return ret;
}

void libmp_exit(int code);
int sys_write(int fd, const void *buf, int n);
int sys_read(int fd, void *buf, int n);
int sys_open(const char *path, int flags);
int sys_close(int fd);
int sys_spawn(const char *path);
int sys_wait(int pid, int *status);
int sys_kill(int pid);
int sys_yield(void);
int sys_time(SysTime *t);
int sys_psinfo(int index, SysProcInfo *out); /* 0 ok, -1 end */
int sys_listdir(const char *path, char *buf, int n);
int sys_unlink(const char *path);
int sys_mkdir(const char *path);
int sys_exists(const char *path);
int sys_chdir(const char *path);
int sys_getcwd(char *buf, int n);
int sys_ioctl(int cmd, int a, int b);
int sys_socket(int type);
int sys_bind(int fd, uint32_t ip, uint16_t port);
int sys_listen(int fd, int backlog);
int sys_accept(int fd);
int sys_connect(int fd, uint32_t ip, uint16_t port);
int sys_send(int fd, const void *buf, int n);
int sys_recv(int fd, void *buf, int n);
uint32_t sys_dns(const char *host);
int sys_ifconfig(char *buf, int n);
int sys_ping(uint32_t ip);

int sys_fb_info(SysFbInfo *info);
int sys_fb_mode(int enable, int w, int h);
int sys_fb_flip(void);
int sys_fb_flip_rect(int x, int y, int w, int h);
int sys_input_poll(SysInputEvent *ev);
int sys_input_field(int field);
int sys_cons_attach(int on);
int sys_cons_hosted(void);
int sys_cons_detach(void);
int sys_cons_read(int pid, char *buf, int n);
int sys_cons_write(int pid, const char *buf, int n);
int sys_cons_putkey(int pid, int key); /* full keycode (arrows 0x100+) */

/* Minimal X-like display server protocol (wm is the server). */
int sys_gui_server(int on);
int sys_gui_create(int w, int h, const char *title);
uint32_t sys_gui_fb(int id);
int sys_gui_damage(int id);
int sys_gui_destroy(int id);
int sys_gui_info(int id, int field);
int sys_gui_next(void);
int sys_gui_ack(int id, int kind); /* 0=map 1=damage */
int sys_gui_find(int pid); /* window id for client process */
int sys_gui_launch(const char *path, const char *argv); /* ask wm to spawn */
int sys_gui_take_launch(char *buf, int n); /* wm: path\\0argv\\0, returns 1 if any */
int sys_gui_title(int id, const char *title);
int sys_gui_get_title(int id, char *buf, int n);
int sys_gui_post(int id, int type, int key);
int sys_gui_post_mouse(int id, int x, int y, int buttons);
int sys_gui_poll(int id, SysInputEvent *ev);
int sys_gui_resize(int id, int w, int h); /* wm only */
int sys_gui_set_flags(int id, int flags);
int sys_beep(int freq_hz, int ms);
int sys_clip_set(const void *buf, int n);
int sys_clip_get(void *buf, int n);
int sys_pipe(int fds[2]);
int sys_dup2_spawn(int in_fd, int out_fd); /* next spawn stdin/stdout fds */

/* display_info field / flags (mirror kernel/gui/display.h) */
enum {
    GUI_INFO_W = 0,
    GUI_INFO_H = 1,
    GUI_INFO_FB = 2,
    GUI_INFO_DAMAGE = 3,
    GUI_INFO_PID = 4,
    GUI_INFO_MAPPED = 5,
    GUI_INFO_FLAGS = 6
};
enum {
    GUI_FLAG_RESIZABLE = 1,
    GUI_FLAG_CLOSE_HOOK = 2, /* client receives INP_CLOSE instead of hard kill */
    GUI_FLAG_NO_MINMAX = 4,  /* hide minimize/maximize caption buttons */
    GUI_FLAG_POPUP = 8       /* no wm chrome / title; client is the whole window */
};

void print(const char *s);
void println(const char *s);
void print_uint(uint32_t v);
void print_ip(uint32_t ip);
int strlen_u(const char *s);
int strcmp_u(const char *a, const char *b);
int strncmp_u(const char *a, const char *b, int n);
void memcpy_u(void *d, const void *s, int n);
void memset_u(void *d, int v, int n);
void strncpy_u(char *d, const char *s, int n);
int atoi_u(const char *s);
uint32_t parse_ip(const char *s);
int read_argv(char *buf, int n);
int split_args(char *line, char **argv, int max);
uint32_t resolve_host(const char *host);
int tcp_connect_host(const char *host, uint16_t port);

#endif
