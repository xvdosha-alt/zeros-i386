#include "libmp.h"

void libmp_exit(int code)
{
    syscall3(SYS_EXIT, code, 0, 0);
    for (;;)
        ;
}

int sys_write(int fd, const void *buf, int n)
{
    return syscall3(SYS_WRITE, fd, (int)buf, n);
}

int sys_read(int fd, void *buf, int n)
{
    return syscall3(SYS_READ, fd, (int)buf, n);
}

int sys_open(const char *path, int flags)
{
    return syscall3(SYS_OPEN, (int)path, flags, 0);
}

int sys_close(int fd)
{
    return syscall3(SYS_CLOSE, fd, 0, 0);
}

int sys_spawn(const char *path)
{
    return syscall3(SYS_SPAWN, (int)path, 0, 0);
}

int sys_wait(int pid, int *status)
{
    return syscall3(SYS_WAIT, pid, (int)status, 0);
}

int sys_kill(int pid)
{
    return syscall3(SYS_KILL, pid, 0, 0);
}

int sys_yield(void)
{
    return syscall3(SYS_YIELD, 0, 0, 0);
}

int sys_time(SysTime *t)
{
    return syscall3(SYS_TIME, (int)t, 0, 0);
}

int sys_psinfo(int index, SysProcInfo *out)
{
    return syscall3(SYS_PSINFO, index, (int)out, 0);
}

int sys_listdir(const char *path, char *buf, int n)
{
    return syscall3(SYS_LISTDIR, (int)path, (int)buf, n);
}

int sys_unlink(const char *path)
{
    return syscall3(SYS_UNLINK, (int)path, 0, 0);
}

int sys_mkdir(const char *path)
{
    return syscall3(SYS_MKDIR, (int)path, 0, 0);
}

int sys_exists(const char *path)
{
    return syscall3(SYS_EXISTS, (int)path, 0, 0);
}

int sys_chdir(const char *path)
{
    return syscall3(SYS_CHDIR, (int)path, 0, 0);
}

int sys_getcwd(char *buf, int n)
{
    return syscall3(SYS_GETCWD, (int)buf, n, 0);
}

int sys_ioctl(int cmd, int a, int b)
{
    return syscall3(SYS_IOCTL, cmd, a, b);
}

int sys_socket(int type)
{
    return syscall3(SYS_SOCKET, type, 0, 0);
}

int sys_bind(int fd, uint32_t ip, uint16_t port)
{
    return syscall3(SYS_BIND, fd, (int)ip, port);
}

int sys_listen(int fd, int backlog)
{
    return syscall3(SYS_LISTEN, fd, backlog, 0);
}

int sys_accept(int fd)
{
    return syscall3(SYS_ACCEPT, fd, 0, 0);
}

int sys_connect(int fd, uint32_t ip, uint16_t port)
{
    return syscall3(SYS_CONNECT, fd, (int)ip, port);
}

int sys_send(int fd, const void *buf, int n)
{
    return syscall3(SYS_SEND, fd, (int)buf, n);
}

int sys_recv(int fd, void *buf, int n)
{
    return syscall3(SYS_RECV, fd, (int)buf, n);
}

uint32_t sys_dns(const char *host)
{
    return (uint32_t)syscall3(SYS_DNS, (int)host, 0, 0);
}

int sys_ifconfig(char *buf, int n)
{
    return syscall3(SYS_IFCONFIG, (int)buf, n, 0);
}

int sys_ping(uint32_t ip)
{
    return syscall3(SYS_PING, (int)ip, 0, 0);
}

int sys_fb_info(SysFbInfo *info)
{
    if (!info) return -1;
    info->width = (uint32_t)syscall3(SYS_FB_INFO, 0, 0, 0);
    info->height = (uint32_t)syscall3(SYS_FB_INFO, 1, 0, 0);
    info->pitch = (uint32_t)syscall3(SYS_FB_INFO, 2, 0, 0);
    info->bpp = (uint32_t)syscall3(SYS_FB_INFO, 3, 0, 0);
    info->addr = (uint32_t)syscall3(SYS_FB_INFO, 4, 0, 0);
    return info->addr ? 0 : -1;
}

int sys_fb_mode(int enable, int w, int h)
{
    return syscall3(SYS_FB_MODE, enable, w, h);
}

int sys_fb_flip(void)
{
    return syscall3(SYS_FB_FLIP, 0, 0, 0);
}

int sys_fb_flip_rect(int x, int y, int w, int h)
{
    uint32_t c;
    if (w < 0)
        w = 0;
    if (h < 0)
        h = 0;
    if (w > 0xffff)
        w = 0xffff;
    if (h > 0xffff)
        h = 0xffff;
    if (w == 0 || h == 0)
        return 0;
    c = ((uint32_t)w & 0xffffu) | (((uint32_t)h & 0xffffu) << 16);
    return syscall3(SYS_FB_FLIP, (uint32_t)x, (uint32_t)y, c);
}

int sys_input_poll(SysInputEvent *ev)
{
    int t = syscall3(SYS_INPUT_POLL, 0, 0, 0);
    if (!ev)
        return t != INP_NONE ? 1 : 0;
    ev->type = (uint32_t)t;
    ev->key = syscall3(SYS_INPUT_FIELD, 0, 0, 0);
    ev->x = syscall3(SYS_INPUT_FIELD, 1, 0, 0);
    ev->y = syscall3(SYS_INPUT_FIELD, 2, 0, 0);
    ev->buttons = syscall3(SYS_INPUT_FIELD, 3, 0, 0);
    ev->dx = syscall3(SYS_INPUT_FIELD, 4, 0, 0);
    ev->dy = syscall3(SYS_INPUT_FIELD, 5, 0, 0);
    return t != INP_NONE ? 1 : 0;
}

int sys_input_field(int field)
{
    return syscall3(SYS_INPUT_FIELD, field, 0, 0);
}

int sys_cons_attach(int on)
{
    return syscall3(SYS_IOCTL, 10, on, 0);
}

int sys_cons_hosted(void)
{
    return syscall3(SYS_IOCTL, 11, 0, 0);
}

int sys_cons_detach(void)
{
    return syscall3(SYS_IOCTL, 12, 0, 0);
}

int sys_cons_read(int pid, char *buf, int n)
{
    return syscall3(SYS_CONS_READ, pid, (int)buf, n);
}

int sys_cons_write(int pid, const char *buf, int n)
{
    return syscall3(SYS_CONS_WRITE, pid, (int)buf, n);
}

int sys_cons_putkey(int pid, int key)
{
    return syscall3(SYS_CONS_WRITE, pid, key, -1);
}

int sys_gui_server(int on)
{
    return syscall3(SYS_GUI_SERVER, on, 0, 0);
}

int sys_gui_create(int w, int h, const char *title)
{
    return syscall3(SYS_GUI_CREATE, w, h, (int)title);
}

uint32_t sys_gui_fb(int id)
{
    return (uint32_t)syscall3(SYS_GUI_FB, id, 0, 0);
}

int sys_gui_damage(int id)
{
    return syscall3(SYS_GUI_DAMAGE, id, 0, 0);
}

int sys_gui_destroy(int id)
{
    return syscall3(SYS_GUI_DESTROY, id, 0, 0);
}

int sys_gui_info(int id, int field)
{
    return syscall3(SYS_GUI_INFO, id, field, 0);
}

int sys_gui_next(void)
{
    return syscall3(SYS_GUI_NEXT, 0, 0, 0);
}

int sys_gui_ack(int id, int kind)
{
    return syscall3(SYS_GUI_ACK, id, kind, 0);
}

int sys_gui_find(int pid)
{
    return syscall3(SYS_GUI_FIND, pid, 0, 0);
}

int sys_gui_launch(const char *path, const char *argv)
{
    return syscall3(SYS_GUI_LAUNCH, (int)path, (int)argv, 0);
}

int sys_gui_take_launch(char *buf, int n)
{
    return syscall3(SYS_GUI_TAKE_LAUNCH, (int)buf, n, 0);
}

int sys_gui_title(int id, const char *title)
{
    return syscall3(SYS_GUI_TITLE, id, (int)title, 0);
}

int sys_gui_get_title(int id, char *buf, int n)
{
    return syscall3(SYS_GUI_TITLE, id, (int)buf, n);
}

int sys_gui_post(int id, int type, int key)
{
    return syscall3(SYS_GUI_POST, id, type, key);
}

int sys_gui_post_mouse(int id, int x, int y, int buttons)
{
    int packed;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x > 0x3FF) x = 0x3FF;
    if (y > 0x3FF) y = 0x3FF;
    packed = (x & 0x3FF) | ((y & 0x3FF) << 10) | ((buttons & 0x3F) << 20);
    return syscall3(SYS_GUI_POST, id, INP_MOUSE, packed);
}

int sys_gui_poll(int id, SysInputEvent *ev)
{
    int t;
    if (!ev)
        return 0;
    t = syscall3(SYS_GUI_POLL, id, 0, 0);
    if (!t)
        return 0;
    ev->type = (uint32_t)t;
    ev->key = sys_input_field(0);
    ev->x = sys_input_field(1);
    ev->y = sys_input_field(2);
    ev->buttons = sys_input_field(3);
    ev->dx = 0;
    ev->dy = 0;
    return t;
}

int sys_gui_resize(int id, int w, int h)
{
    return syscall3(SYS_GUI_RESIZE, id, w, h);
}

int sys_gui_set_flags(int id, int flags)
{
    return syscall3(SYS_GUI_FLAGS, id, flags, 0);
}

int sys_beep(int freq_hz, int ms)
{
    return syscall3(SYS_BEEP, freq_hz, ms, 0);
}

int sys_clip_set(const void *buf, int n)
{
    return syscall3(SYS_CLIP_SET, (int)buf, n, 0);
}

int sys_clip_get(void *buf, int n)
{
    return syscall3(SYS_CLIP_GET, (int)buf, n, 0);
}

int sys_pipe(int fds[2])
{
    return syscall3(SYS_PIPE, (int)fds, 0, 0);
}

int sys_dup2_spawn(int in_fd, int out_fd)
{
    return syscall3(SYS_DUP2, in_fd, out_fd, 0);
}

void print(const char *s)
{
    sys_write(1, s, strlen_u(s));
}

void println(const char *s)
{
    print(s);
    sys_write(1, "\n", 1);
}

void print_uint(uint32_t v)
{
    char tmp[12];
    int i = 0, n = 0;
    if (v == 0) {
        sys_write(1, "0", 1);
        return;
    }
    while (v) {
        tmp[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (i)
        tmp[n++] = tmp[--i];
    sys_write(1, tmp, n);
}

void print_ip(uint32_t ip)
{
    print_uint((ip >> 24) & 255);
    sys_write(1, ".", 1);
    print_uint((ip >> 16) & 255);
    sys_write(1, ".", 1);
    print_uint((ip >> 8) & 255);
    sys_write(1, ".", 1);
    print_uint(ip & 255);
}

int strlen_u(const char *s)
{
    int n = 0;
    while (s[n])
        n++;
    return n;
}

int strcmp_u(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp_u(const char *a, const char *b, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        if (a[i] != b[i])
            return (unsigned char)a[i] - (unsigned char)b[i];
        if (!a[i])
            return 0;
    }
    return 0;
}

void memcpy_u(void *d, const void *s, int n)
{
    char *D = (char *)d;
    const char *S = (const char *)s;
    while (n--)
        *D++ = *S++;
}

void memset_u(void *d, int v, int n)
{
    char *D = (char *)d;
    while (n--)
        *D++ = (char)v;
}

void strncpy_u(char *d, const char *s, int n)
{
    int i;
    for (i = 0; i + 1 < n && s[i]; i++)
        d[i] = s[i];
    d[i] = 0;
}

int atoi_u(const char *s)
{
    int v = 0;
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s - '0');
        s++;
    }
    return v;
}

uint32_t parse_ip(const char *s)
{
    int a = 0, b = 0, c = 0, d = 0, part = 0, v = 0;
    int saw = 0;
    if (!s || !s[0])
        return 0;
    while (*s) {
        if (*s >= '0' && *s <= '9') {
            v = v * 10 + (*s - '0');
            saw = 1;
        } else if (*s == '.') {
            if (part == 0)
                a = v;
            else if (part == 1)
                b = v;
            else if (part == 2)
                c = v;
            v = 0;
            part++;
        } else
            return 0;
        s++;
    }
    if (!saw || part != 3)
        return 0;
    d = v;
    return ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)c << 8) | (uint32_t)d;
}

int read_argv(char *buf, int n)
{
    int fd, r;
    if (!buf || n <= 0)
        return 0;
    buf[0] = 0;
    if (!sys_exists("/sys/run/argv"))
        return 0;
    fd = sys_open("/sys/run/argv", 1);
    if (fd < 0)
        return 0;
    r = sys_read(fd, buf, n - 1);
    sys_close(fd);
    if (r < 0)
        r = 0;
    buf[r] = 0;
    return r;
}

int split_args(char *line, char **argv, int max)
{
    int argc = 0, i = 0;
    while (line[i] && argc + 1 < max) {
        while (line[i] == ' ' || line[i] == '\t')
            i++;
        if (!line[i])
            break;
        argv[argc++] = &line[i];
        while (line[i] && line[i] != ' ' && line[i] != '\t')
            i++;
        if (line[i])
            line[i++] = 0;
    }
    argv[argc] = 0;
    return argc;
}

uint32_t resolve_host(const char *host)
{
    uint32_t ip;
    if (!host || !host[0])
        return 0;
    ip = parse_ip(host);
    if (ip)
        return ip;
    return sys_dns(host);
}

int tcp_connect_host(const char *host, uint16_t port)
{
    uint32_t ip;
    int s;
    ip = resolve_host(host);
    if (!ip)
        return -1;
    s = sys_socket(2);
    if (s < 0)
        return -2;
    if (sys_connect(s, ip, port) < 0) {
        sys_close(s);
        return -3;
    }
    return s;
}
