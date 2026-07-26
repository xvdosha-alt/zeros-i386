#include "libmp.h"

int main(void)
{
    char path[128];
    char buf[256];
    int fd, n;
    path[0] = 0;
    if (sys_exists("/sys/run/argv")) {
        fd = sys_open("/sys/run/argv", 1);
        if (fd >= 0) {
            n = sys_read(fd, path, sizeof(path) - 1);
            sys_close(fd);
            if (n > 0)
                path[n] = 0;
        }
    }
    {
        int j = 0;
        while (path[j] && path[j] != ' ')
            j++;
        path[j] = 0;
    }
    if (!path[0]) {
        println("usage: cat PATH");
        return 1;
    }
    fd = sys_open(path, 1);
    if (fd < 0) {
        println("cat: open fail");
        return 1;
    }
    {
        int last = '\n';
        while ((n = sys_read(fd, buf, sizeof(buf))) > 0) {
            sys_write(1, buf, n);
            last = (unsigned char)buf[n - 1];
        }
        sys_close(fd);
        if (last != '\n')
            sys_write(1, "\n", 1);
    }
    return 0;
}
