#include "libmp.h"

#define O_READ 1
#define O_WRITE 2
#define O_TRUNC 8
#define O_CREATE 16

static char text[4096];
static int len;
static char path[128];
static int dirty;

static void load(void)
{
    int fd = sys_open(path, O_READ);
    len = 0;
    dirty = 0;
    if (fd < 0)
        return;
    len = sys_read(fd, text, sizeof(text) - 1);
    if (len < 0)
        len = 0;
    text[len] = 0;
    sys_close(fd);
}

static int save(void)
{
    int fd = sys_open(path, O_WRITE | O_CREATE | O_TRUNC);
    if (fd < 0) {
        println("save fail");
        return -1;
    }
    sys_write(fd, text, len);
    sys_close(fd);
    dirty = 0;
    println("");
    print("[saved ");
    print(path);
    println("]");
    return 0;
}

static void redraw(void)
{
    sys_ioctl(1, 0, 0);
    print("nano ");
    print(path);
    if (dirty)
        print(" *");
    println("");
    println("---- F2/Ctrl+S save   F3/Ctrl+X exit ----");
    sys_write(1, text, len);
}

int main(void)
{
    int fd, n, key;
    strncpy_u(path, "/sys/tmp/note.txt", sizeof(path));
    if (sys_exists("/sys/run/argv")) {
        fd = sys_open("/sys/run/argv", O_READ);
        if (fd >= 0) {
            n = sys_read(fd, path, sizeof(path) - 1);
            sys_close(fd);
            if (n > 0) {
                path[n] = 0;
                while (n > 0 && (path[n - 1] == '\n' || path[n - 1] == ' '))
                    path[--n] = 0;
            }
        }
    }
    load();
    redraw();
    for (;;) {
        key = 0;
        if (sys_read(0, &key, 4) != 4)
            continue;
        if (key == 24 || key == 3) {
            if (dirty)
                save();
            sys_write(1, "\n", 1);
            return 0;
        }
        if (key == 19 || key == 15) {
            save();
            redraw();
            continue;
        }
        if (key == '\b' || key == 127) {
            if (len > 0) {
                text[--len] = 0;
                dirty = 1;
            }
            redraw();
            continue;
        }
        if (key >= 0x100)
            continue;
        if (key >= 32 || key == '\n' || key == '\t' || key == '\r') {
            if (key == '\r')
                key = '\n';
            if (len + 1 < (int)sizeof(text)) {
                char c = (char)key;
                text[len++] = c;
                text[len] = 0;
                dirty = 1;
                if (c == '\n')
                    redraw();
                else
                    sys_write(1, &c, 1);
            }
        }
    }
}
