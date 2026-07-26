#include "libmp.h"

#define O_READ 1
#define O_WRITE 2
#define O_CREATE 16
#define O_TRUNC 8

static void list_repo(void)
{
    char buf[512];
    println("packages:");
    if (sys_listdir("/sys/pkg/repo", buf, sizeof(buf)) >= 0)
        print(buf);
}

static void install_pkg(const char *name)
{
    char src[128];
    char dst[128];
    char data[2048];
    int fd, n;
    strncpy_u(src, "/sys/pkg/repo/", sizeof(src));
    strncpy_u(src + 14, name, (int)sizeof(src) - 14);
    if (!sys_exists(src)) {
        println("pkg: not in repo");
        return;
    }
    fd = sys_open(src, O_READ);
    if (fd < 0)
        return;
    n = sys_read(fd, data, sizeof(data));
    sys_close(fd);
    if (n < 0)
        return;
    strncpy_u(dst, "/sys/bin/", sizeof(dst));
    strncpy_u(dst + 9, name, (int)sizeof(dst) - 9);
    fd = sys_open(dst, O_WRITE | O_CREATE | O_TRUNC);
    if (fd < 0) {
        println("pkg: install fail");
        return;
    }
    sys_write(fd, data, n);
    sys_close(fd);
    print("installed ");
    println(name);
}

static void remove_pkg(const char *name)
{
    char path[128];
    strncpy_u(path, "/sys/bin/", sizeof(path));
    strncpy_u(path + 9, name, (int)sizeof(path) - 9);
    if (sys_unlink(path) < 0)
        println("pkg: remove fail");
    else
        println("removed");
}

int main(void)
{
    char arg[128];
    char cmd[32];
    char name[64];
    int fd, n, i;
    arg[0] = 0;
    if (sys_exists("/sys/run/argv")) {
        fd = sys_open("/sys/run/argv", O_READ);
        if (fd >= 0) {
            n = sys_read(fd, arg, sizeof(arg) - 1);
            sys_close(fd);
            if (n > 0)
                arg[n] = 0;
        }
    }
    cmd[0] = 0;
    name[0] = 0;
    i = 0;
    while (arg[i] && arg[i] != ' ' && i + 1 < (int)sizeof(cmd)) {
        cmd[i] = arg[i];
        i++;
    }
    cmd[i] = 0;
    while (arg[i] == ' ')
        i++;
    strncpy_u(name, arg + i, sizeof(name));
    if (!cmd[0] || !strcmp_u(cmd, "list"))
        list_repo();
    else if (!strcmp_u(cmd, "install"))
        install_pkg(name);
    else if (!strcmp_u(cmd, "remove"))
        remove_pkg(name);
    else
        println("pkg list|install|remove NAME");
    return 0;
}
