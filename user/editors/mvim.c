#include "libmp.h"

#define O_READ 1
#define O_WRITE 2
#define O_TRUNC 8
#define O_CREATE 16

static char text[4096];
static int len;
static int cursor;
static int mode;
static char path[128];
static char cmd[64];
static int cmdlen;

static void load(void)
{
    int fd = sys_open(path, O_READ);
    len = 0;
    cursor = 0;
    if (fd < 0)
        return;
    len = sys_read(fd, text, sizeof(text) - 1);
    if (len < 0)
        len = 0;
    text[len] = 0;
    sys_close(fd);
}

static void save(void)
{
    int fd = sys_open(path, O_WRITE | O_CREATE | O_TRUNC);
    if (fd >= 0) {
        sys_write(fd, text, len);
        sys_close(fd);
    }
}

static void redraw(void)
{
    sys_ioctl(1, 0, 0);
    print(mode ? "-- INSERT -- " : "-- NORMAL -- ");
    println(path);
    sys_write(1, text, len);
    println("");
    if (mode == 2) {
        print(":");
        sys_write(1, cmd, cmdlen);
    }
}

int main(void)
{
    int fd, n, c;
    strncpy_u(path, "/sys/tmp/note.txt", sizeof(path));
    if (sys_exists("/sys/run/argv")) {
        fd = sys_open("/sys/run/argv", O_READ);
        if (fd >= 0) {
            n = sys_read(fd, path, sizeof(path) - 1);
            sys_close(fd);
            if (n > 0)
                path[n] = 0;
        }
    }
    load();
    mode = 0;
    redraw();
    for (;;) {
        c = 0;
        if (sys_read(0, &c, 1) != 1)
            continue;
        if (mode == 2) {
            if (c == '\n' || c == '\r') {
                cmd[cmdlen] = 0;
                if (!strcmp_u(cmd, "w"))
                    save();
                else if (!strcmp_u(cmd, "q"))
                    return 0;
                else if (!strcmp_u(cmd, "wq")) {
                    save();
                    return 0;
                }
                mode = 0;
                cmdlen = 0;
                redraw();
                continue;
            }
            if ((c == '\b' || c == 127) && cmdlen > 0)
                cmdlen--;
            else if (c >= 32 && cmdlen + 1 < (int)sizeof(cmd))
                cmd[cmdlen++] = (char)c;
            redraw();
            continue;
        }
        if (mode == 1) {
            if (c == 27) {
                mode = 0;
                redraw();
                continue;
            }
            if (c == '\b' || c == 127) {
                if (cursor > 0) {
                    int i;
                    for (i = cursor - 1; i < len; i++)
                        text[i] = text[i + 1];
                    len--;
                    cursor--;
                }
                redraw();
                continue;
            }
            if (len + 1 < (int)sizeof(text)) {
                int i;
                for (i = len; i > cursor; i--)
                    text[i] = text[i - 1];
                text[cursor++] = (char)c;
                len++;
                text[len] = 0;
                redraw();
            }
            continue;
        }
        if (c == 'i') {
            mode = 1;
            redraw();
        } else if (c == 'a') {
            mode = 1;
            if (cursor < len)
                cursor++;
            redraw();
        } else if (c == 'h' && cursor > 0)
            cursor--;
        else if (c == 'l' && cursor < len)
            cursor++;
        else if (c == '0')
            cursor = 0;
        else if (c == '$')
            cursor = len;
        else if (c == 'x' && cursor < len) {
            int i;
            for (i = cursor; i < len; i++)
                text[i] = text[i + 1];
            len--;
            redraw();
        } else if (c == 'd') {
            len = 0;
            cursor = 0;
            text[0] = 0;
            redraw();
        } else if (c == ':') {
            mode = 2;
            cmdlen = 0;
            redraw();
        } else if (c == '/') {
            mode = 2;
            cmd[0] = '/';
            cmdlen = 1;
            redraw();
        }
    }
}
