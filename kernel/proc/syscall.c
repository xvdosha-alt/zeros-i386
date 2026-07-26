#include "syscall.h"
#include "proc.h"
#include "vfs.h"
#include "tty.h"
#include "string.h"
#include "net.h"
#include "fb.h"
#include "mouse.h"
#include "kbd.h"
#include "io.h"
#include "display.h"
#include "rtc.h"
#include "pcspeaker.h"

static SysInputEvent g_input_ev;

#define CLIP_MAX 4096
static char g_clip[CLIP_MAX];
static int g_clip_len;

#define PIPE_MAX 8
#define PIPE_BUF 2048
typedef struct {
    int used;
    char buf[PIPE_BUF];
    int len;
    int rpos;
    int rfd;
    int wfd;
} KPipe;
static KPipe g_pipes[PIPE_MAX];

static int pipe_from_fd(int fd)
{
    int i;
    for (i = 0; i < PIPE_MAX; i++) {
        if (!g_pipes[i].used)
            continue;
        if (g_pipes[i].rfd == fd || g_pipes[i].wfd == fd)
            return i;
    }
    return -1;
}

static int pipe_read(int pi, void *buf, int n)
{
    KPipe *p;
    int got = 0;
    char *out = (char *)buf;
    if (pi < 0 || !buf || n <= 0)
        return -1;
    p = &g_pipes[pi];
    while (got < n && p->len > 0) {
        out[got++] = p->buf[p->rpos];
        p->rpos = (p->rpos + 1) % PIPE_BUF;
        p->len--;
    }
    return got;
}

static int pipe_write(int pi, const void *buf, int n)
{
    KPipe *p;
    int put = 0;
    const char *in = (const char *)buf;
    if (pi < 0 || !buf || n <= 0)
        return -1;
    p = &g_pipes[pi];
    while (put < n && p->len < PIPE_BUF) {
        int wpos = (p->rpos + p->len) % PIPE_BUF;
        p->buf[wpos] = in[put++];
        p->len++;
    }
    return put;
}

static int pipe_close_fd(int fd)
{
    int pi = pipe_from_fd(fd);
    if (pi < 0)
        return -1;
    if (g_pipes[pi].rfd == fd)
        g_pipes[pi].rfd = -1;
    if (g_pipes[pi].wfd == fd)
        g_pipes[pi].wfd = -1;
    if (g_pipes[pi].rfd < 0 && g_pipes[pi].wfd < 0)
        g_pipes[pi].used = 0;
    return 0;
}

static char *cur_cwd(void)
{
    Proc *c = proc_current();
    if (!c)
        return 0;
    if (!c->cwd[0])
        kstrncpy(c->cwd, "/sys", sizeof(c->cwd));
    return c->cwd;
}

static void abs_path(const char *in, char *out, size_t n)
{
    char joined[VFS_PATH_MAX];
    const char *cwd = cur_cwd();
    if (!cwd)
        cwd = "/";
    if (!in || !in[0]) {
        vfs_normalize(out, cwd, n);
        return;
    }
    if (in[0] == '/') {
        vfs_normalize(out, in, n);
        return;
    }
    if (!kstrcmp(cwd, "/")) {
        joined[0] = '/';
        kstrncpy(joined + 1, in, sizeof(joined) - 1);
    } else {
        size_t L = kstrlen(cwd);
        kstrncpy(joined, cwd, sizeof(joined));
        if (L + 1 < sizeof(joined))
            joined[L] = '/';
        kstrncpy(joined + L + 1, in, sizeof(joined) - L - 1);
    }
    vfs_normalize(out, joined, n);
}

uint32_t syscall_handler(uint32_t *frame)
{
    uint32_t num = frame[7];
    uint32_t a = frame[4];
    uint32_t b = frame[6];
    uint32_t c = frame[5];
    char path[VFS_PATH_MAX];
    int r;

    switch (num) {
    case SYS_EXIT:
        proc_exit((int)a);
        return 0;
    case SYS_WRITE:
        if (a == 1 || a == 2) {
            const char *s = (const char *)b;
            size_t i;
            Proc *cur = proc_current();
            int pi;
            if (cur && cur->redir_out >= 0) {
                pi = pipe_from_fd(cur->redir_out);
                if (pi >= 0)
                    return (uint32_t)pipe_write(pi, s, (int)c);
                return (uint32_t)vfs_write(cur->redir_out, s, (size_t)c);
            }
            proc_cons_repair(cur);
            if (cur && cur->cons_enable) {
                Proc *sink;
                int written = proc_cons_write(cur, s, (int)c);
                int has_nl = 0;
                sink = cur->cons_sink ? proc_get(cur->cons_sink) : cur;
                if (!sink)
                    sink = cur;
                for (i = 0; i < (size_t)c; i++) {
                    if (s[i] == '\n') {
                        has_nl = 1;
                        break;
                    }
                }
                /* Win3.1 DOS box: VM yields so the host can blit the screen.
                 * Safe now that coop-yield restores the full trap frame. */
                if (has_nl || sink->cons_out_len + 64 >= PROC_CONS_OUT ||
                    (cur && cur->need_preempt)) {
                    if (cur)
                        cur->need_preempt = 0;
                    frame[7] = (uint32_t)written;
                    proc_yield_to_parent(frame);
                }
                return (uint32_t)written;
            }
            for (i = 0; i < (size_t)c; i++)
                tty_putc(s[i]);
            return c;
        }
        {
            int pi = pipe_from_fd((int)a);
            if (pi >= 0)
                return (uint32_t)pipe_write(pi, (const void *)b, (int)c);
        }
        return (uint32_t)vfs_write((int)a, (const void *)b, (size_t)c);
    case SYS_READ:
        if (a == 0) {
            Proc *cur = proc_current();
            int ch;
            int pi;
            if (cur && cur->redir_in >= 0) {
                char tmp;
                int got;
                pi = pipe_from_fd(cur->redir_in);
                if (pi >= 0)
                    got = pipe_read(pi, &tmp, 1);
                else
                    got = vfs_read(cur->redir_in, &tmp, 1);
                if (got <= 0) {
                    if (pi >= 0 && g_pipes[pi].wfd < 0)
                        return 0; /* EOF */
                    frame[7] = 0;
                    proc_yield_to_parent(frame);
                    return 0;
                }
                if (b && c >= 4) {
                    *(int *)b = (unsigned char)tmp;
                    return 4;
                }
                if (b && c > 0) {
                    ((char *)b)[0] = tmp;
                    return 1;
                }
                return (uint32_t)(unsigned char)tmp;
            }
            proc_cons_repair(cur);
            if (cur && cur->cons_enable) {
                ch = proc_cons_getc(cur);
                if (ch < 0) {
                    frame[7] = 0;
                    proc_yield_to_parent(frame);
                    return 0;
                }
                if (b && c >= 4) {
                    *(int *)b = ch;
                    return 4;
                }
                if (b && c > 0) {
                    ((char *)b)[0] = (char)ch;
                    return 1;
                }
                return (uint32_t)ch;
            }
            ch = tty_getc();
            if (b && c >= 4) {
                *(int *)b = ch;
                return 4;
            }
            if (b && c > 0) {
                ((char *)b)[0] = (char)ch;
                return 1;
            }
            return (uint32_t)ch;
        }
        {
            int pi = pipe_from_fd((int)a);
            if (pi >= 0)
                return (uint32_t)pipe_read(pi, (void *)b, (int)c);
        }
        return (uint32_t)vfs_read((int)a, (void *)b, (size_t)c);
    case SYS_OPEN:
        abs_path((const char *)a, path, sizeof(path));
        return (uint32_t)vfs_open(path, (int)b);
    case SYS_CLOSE: {
        int vr;
        if (pipe_close_fd((int)a) == 0)
            return 0;
        vr = vfs_close((int)a);
        if (vr == 0)
            return 0;
        
        if (net_close((int)a) == 0)
            return 0;
        return (uint32_t)vr;
    }
    case SYS_SPAWN:
        abs_path((const char *)a, path, sizeof(path));
        return (uint32_t)proc_spawn_elf(path, 0, 0);
    case SYS_WAIT: {
        int st = 0;
        r = proc_wait((int)a, &st);
        if (b)
            *(int *)b = st;
        return (uint32_t)r;
    }
    case SYS_YIELD:
        /* Console-hosted: return the slice to the waiter. HLT one tick
         * so animating clients cannot zero-delay spin the parent. */
        {
            Proc *cur = proc_current();
            if (cur)
                cur->need_preempt = 0;
            if (cur && cur->cons_enable) {
                __asm__ volatile ("sti; hlt");
                proc_yield_to_parent(frame);
                return 0;
            }
        }
        __asm__ volatile ("sti; hlt");
        return 0;
    case SYS_LISTDIR:
        abs_path((const char *)a, path, sizeof(path));
        return (uint32_t)vfs_listdir(path, (char *)b, (size_t)c);
    case SYS_UNLINK:
        abs_path((const char *)a, path, sizeof(path));
        r = vfs_unlink(path);
        if (r == 0) {
            size_t pl = kstrlen(path);
            int i;
            /* Any process whose cwd vanished falls back to /. */
            for (i = 0; i < PROC_MAX; i++) {
                Proc *p = proc_get_by_index(i);
                char *pc;
                if (!p || !p->used)
                    continue;
                pc = p->cwd;
                if (!pc[0])
                    continue;
                if (!kstrcmp(pc, path) ||
                    (pl > 1 && !kstrncmp(pc, path, pl) && pc[pl] == '/'))
                    kstrncpy(pc, "/", PROC_CWD);
            }
        }
        return (uint32_t)r;
    case SYS_MKDIR:
        abs_path((const char *)a, path, sizeof(path));
        return (uint32_t)vfs_mkdir(path);
    case SYS_EXISTS:
        abs_path((const char *)a, path, sizeof(path));
        return vfs_exists(path) ? 1 : 0;
    case SYS_GETPID:
        return (uint32_t)proc_current()->pid;
    case SYS_CHDIR: {
        char *cwd = cur_cwd();
        abs_path((const char *)a, path, sizeof(path));
        if (!vfs_isdir(path))
            return (uint32_t)-1;
        if (!cwd)
            return (uint32_t)-1;
        kstrncpy(cwd, path, PROC_CWD);
        return 0;
    }
    case SYS_GETCWD: {
        char *cwd = cur_cwd();
        if (!cwd)
            return (uint32_t)-1;
        kstrncpy((char *)a, cwd, (size_t)b);
        return 0;
    }
    case SYS_IOCTL:
        if (a == 1) {
            tty_clear();
            return 0;
        }
        if (a == 2) {
            tty_goto((int)b, (int)c);
            return 0;
        }
        if (a == 3) {
            int row = 0, col = 0;
            tty_get_cursor(&row, &col);
            return (uint32_t)(((uint32_t)row << 16) | (uint32_t)(col & 0xFFFF));
        }
        /* 10: next spawn attaches a GUI console (stdout/stdin rings). */
        if (a == 10) {
            proc_set_spawn_cons((int)b);
            return 0;
        }
        /* 11: am I hosted in a GUI console? (DOS-box style) */
        if (a == 11) {
            Proc *cur = proc_current();
            return (uint32_t)(cur && cur->cons_enable ? 1 : 0);
        }
        /* 12: drop console-hosting on this process (wm must not yield
         * to desktop on every println when launched from msh). */
        if (a == 12) {
            Proc *cur = proc_current();
            if (cur) {
                cur->cons_enable = 0;
                cur->cons_host = 0;
                cur->cons_sink = 0;
                cur->cons_forbid = 1;
            }
            return 0;
        }
        return (uint32_t)-1;
    case SYS_SOCKET:
        return (uint32_t)net_socket((int)a);
    case SYS_BIND:
        return (uint32_t)net_bind((int)a, (uint32_t)b, (uint16_t)c);
    case SYS_LISTEN:
        return (uint32_t)net_listen((int)a, (int)b);
    case SYS_ACCEPT:
        return (uint32_t)net_accept((int)a);
    case SYS_CONNECT:
        return (uint32_t)net_connect((int)a, (uint32_t)b, (uint16_t)c);
    case SYS_SEND:
        return (uint32_t)net_send((int)a, (const void *)b, (size_t)c);
    case SYS_RECV:
        return (uint32_t)net_recv((int)a, (void *)b, (size_t)c);
    case SYS_DNS:
        return net_dns((const char *)a);
    case SYS_IFCONFIG:
        return net_ifconfig((char *)a, (size_t)b);
    case SYS_PING:
        return (uint32_t)net_ping((uint32_t)a);
    case SYS_FB_INFO: {
        FbInfo info;
        if (!fb_ready())
            return 0;
        fb_info(&info);
        if (a == 0) return info.width;
        if (a == 1) return info.height;
        if (a == 2) return info.pitch;
        if (a == 3) return info.bpp;
        if (a == 4) return info.addr;
        return info.addr;
    }
    case SYS_FB_MODE:
        /* Display server owns the LFB — clients use SYS_GUI_* windows. */
        if (display_fb_locked()) {
            Proc *cur = proc_current();
            if (!cur || cur->pid != display_server_pid())
                return (uint32_t)-1;
        }
        if (a == 0) {
            fb_textmode();
            return 0;
        }
        {
            uint32_t w = b ? b : 1024;
            uint32_t h = c ? c : 768;
            uint32_t addr;
            if (fb_init(w, h, 32) != 0)
                return (uint32_t)-1;
            mouse_set_bounds((int)w, (int)h);
            /* Return the draw surface (backbuffer when available). */
            addr = (uint32_t)(uintptr_t)fb_ptr();
            return addr ? addr : (uint32_t)-1;
        }
    case SYS_FB_FLIP:
        /* c == 0: full flip. Else rect: a=x, b=y, c=(w)|(h<<16). */
        if (c == 0) {
            fb_flip();
        } else {
            fb_flip_rect((int)a, (int)b,
                         (int)(c & 0xffffu), (int)((c >> 16) & 0xffffu));
        }
        return 0;
    case SYS_INPUT_POLL: {
        static int last_buttons;
        static int last_x = -1;
        static int last_y = -1;
        int x, y, buttons;
        int guard;

        g_input_ev.type = INP_NONE;
        g_input_ev.key = 0;
        g_input_ev.dx = 0;
        g_input_ev.dy = 0;
        g_input_ev.x = last_x < 0 ? 512 : last_x;
        g_input_ev.y = last_y < 0 ? 384 : last_y;
        g_input_ev.buttons = last_buttons;

        /* Drain PS/2 under CLI (shared kbd/mouse buffer). */
        guard = 64;
        while (guard-- > 0) {
            uint8_t st = inb(0x64);
            if (!(st & 1))
                break;
            if (st & 0x20)
                mouse_byte(inb(0x60));
            else
                kbd_irq_handler();
        }

        /*
         * Do not poll serial LSR (0x3FD) while framebuffer is active:
         * after VBE mode set, reading it can wedge the guest forever.
         */
        if (!fb_ready() && (inb(0x3F8 + 5) & 0x01) != 0) {
            unsigned char ch = inb(0x3F8);
            if (ch == 0x7F) ch = '\b';
            if (ch == '\r') ch = '\n';
            g_input_ev.type = INP_KEY;
            g_input_ev.key = (int)ch;
            return INP_KEY;
        }
        if (kbd_pending()) {
            g_input_ev.type = INP_KEY;
            g_input_ev.key = kbd_pop();
            return INP_KEY;
        }

        mouse_coords(&x, &y, &buttons);
        g_input_ev.x = x;
        g_input_ev.y = y;
        g_input_ev.buttons = buttons;
        if (last_x < 0) {
            last_x = x;
            last_y = y;
            last_buttons = buttons;
            return INP_NONE;
        }
        if (x != last_x || y != last_y || buttons != last_buttons) {
            g_input_ev.type = INP_MOUSE;
            g_input_ev.dx = x - last_x;
            g_input_ev.dy = y - last_y;
            last_x = x;
            last_y = y;
            last_buttons = buttons;
            return INP_MOUSE;
        }
        return INP_NONE;
    }
    case SYS_INPUT_FIELD:
        if (a == 0) return (uint32_t)g_input_ev.key;
        if (a == 1) return (uint32_t)g_input_ev.x;
        if (a == 2) return (uint32_t)g_input_ev.y;
        if (a == 3) return (uint32_t)g_input_ev.buttons;
        if (a == 4) return (uint32_t)g_input_ev.dx;
        if (a == 5) return (uint32_t)g_input_ev.dy;
        return 0;
    case SYS_CONS_READ:
        return (uint32_t)proc_cons_read((int)a, (char *)b, (int)c);
    case SYS_CONS_WRITE:
        if ((int)c < 0)
            return (uint32_t)proc_cons_putkey((int)a, (int)b);
        return (uint32_t)proc_cons_write_in((int)a, (const char *)b, (int)c);
    case SYS_GUI_SERVER:
        return (uint32_t)display_server_set((int)a);
    case SYS_GUI_CREATE:
        return (uint32_t)display_create((int)a, (int)b, (const char *)c);
    case SYS_GUI_FB:
        return display_fb((int)a);
    case SYS_GUI_DAMAGE:
        return (uint32_t)display_damage((int)a, frame);
    case SYS_GUI_DESTROY:
        return (uint32_t)display_destroy((int)a);
    case SYS_GUI_INFO:
        return (uint32_t)display_info((int)a, (int)b);
    case SYS_GUI_NEXT:
        return (uint32_t)display_next_new();
    case SYS_GUI_ACK:
        /* b=0 map, b=1 damage */
        if ((int)b == 0)
            return (uint32_t)display_ack_map((int)a);
        return (uint32_t)display_ack_damage((int)a);
    case SYS_GUI_LAUNCH:
        return (uint32_t)display_request_launch((const char *)a, (const char *)b);
    case SYS_GUI_TAKE_LAUNCH:
        return (uint32_t)display_take_launch((char *)a, (int)b);
    case SYS_GUI_FIND:
        return (uint32_t)display_find_pid((int)a);
    case SYS_GUI_TITLE:
        if ((int)c > 0)
            return (uint32_t)display_get_title((int)a, (char *)b, (int)c);
        return (uint32_t)display_set_title((int)a, (const char *)b);
    case SYS_GUI_POST:
        /* KEY: c=key; MOUSE: c packs x|y<<10|buttons<<20 */
        if ((uint32_t)b == INP_MOUSE) {
            int packed = (int)c;
            int mx = packed & 0x3FF;
            int my = (packed >> 10) & 0x3FF;
            int mb = (packed >> 20) & 0x3F;
            return (uint32_t)display_post_event((int)a, INP_MOUSE, 0, mx, my, mb);
        }
        return (uint32_t)display_post_event((int)a, (uint32_t)b, (int)c, 0, 0, 0);
    case SYS_GUI_POLL: {
        uint32_t t = 0;
        int key = 0, x = 0, y = 0, buttons = 0;
        if (!display_poll_event((int)a, &t, &key, &x, &y, &buttons))
            return 0;
        g_input_ev.type = t;
        g_input_ev.key = key;
        g_input_ev.x = x;
        g_input_ev.y = y;
        g_input_ev.buttons = buttons;
        return t;
    }
    case SYS_KILL:
        return (uint32_t)proc_kill((int)a);
    case SYS_TIME: {
        RtcTime rt;
        if (!a)
            return (uint32_t)-1;
        if (rtc_read(&rt) != 0)
            return (uint32_t)-1;
        kmemcpy((void *)a, &rt, sizeof(rt));
        return 0;
    }
    case SYS_PSINFO: {
        int pid, ppid, state;
        char name[32];
        int *u;
        if (!b)
            return (uint32_t)-1;
        if (proc_psinfo((int)a, &pid, &ppid, &state, name, (int)sizeof(name)) != 0)
            return (uint32_t)-1;
        /* Layout matches SysProcInfo: pid, ppid, state, name[32] */
        u = (int *)b;
        u[0] = pid;
        u[1] = ppid;
        u[2] = state;
        kmemcpy((char *)b + 12, name, 32);
        return 0;
    }
    case SYS_GUI_RESIZE:
        return (uint32_t)display_resize((int)a, (int)b, (int)c);
    case SYS_GUI_FLAGS:
        return (uint32_t)display_set_flags((int)a, (int)b);
    case SYS_BEEP:
        pcspeaker_beep((uint32_t)a, (uint32_t)b);
        return 0;
    case SYS_CLIP_SET: {
        int n = (int)b;
        if (!a || n < 0)
            return (uint32_t)-1;
        if (n > CLIP_MAX)
            n = CLIP_MAX;
        kmemcpy(g_clip, (const void *)a, (size_t)n);
        g_clip_len = n;
        return (uint32_t)n;
    }
    case SYS_CLIP_GET: {
        int n = (int)b;
        if (!a || n < 0)
            return (uint32_t)-1;
        if (n > g_clip_len)
            n = g_clip_len;
        if (n > 0)
            kmemcpy((void *)a, g_clip, (size_t)n);
        return (uint32_t)n;
    }
    case SYS_PIPE: {
        int i, rfd, wfd;
        int *out = (int *)a;
        if (!out)
            return (uint32_t)-1;
        for (i = 0; i < PIPE_MAX; i++) {
            if (!g_pipes[i].used)
                break;
        }
        if (i >= PIPE_MAX)
            return (uint32_t)-1;
        /* Reserve high fd numbers for pipes (avoid clash with vfs 3..31). */
        rfd = 48 + i * 2;
        wfd = rfd + 1;
        kmemset(&g_pipes[i], 0, sizeof(g_pipes[i]));
        g_pipes[i].used = 1;
        g_pipes[i].rfd = rfd;
        g_pipes[i].wfd = wfd;
        out[0] = rfd;
        out[1] = wfd;
        return 0;
    }
    case SYS_DUP2:
        /* a = in_fd for next spawn stdin, b = out_fd for next spawn stdout.
         * Pass -1 to leave default. */
        proc_set_spawn_redir((int)a, (int)b);
        return 0;
    default:
        return (uint32_t)-1;
    }
}

void syscall_init(void)
{
}

void sched_loop(void)
{
    for (;;)
        __asm__ volatile ("sti; hlt");
}
