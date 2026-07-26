#include "libmp.h"

#define O_READ 1
#define O_WRITE 2
#define O_TRUNC 8
#define O_CREATE 16

static int is_app(const char *path)
{
    char hdr[20];
    int fd, n;
    unsigned short type, mach;

    fd = sys_open(path, O_READ);
    if (fd < 0)
        return 0;
    n = sys_read(fd, hdr, (int)sizeof(hdr));
    sys_close(fd);
    if (n < 20)
        return 0;
    if ((unsigned char)hdr[0] != 0x7F || hdr[1] != 'E' ||
        hdr[2] != 'L' || hdr[3] != 'F')
        return 0;
    type = (unsigned char)hdr[16] | ((unsigned char)hdr[17] << 8);
    mach = (unsigned char)hdr[18] | ((unsigned char)hdr[19] << 8);
    /* ET_EXEC=2, EM_386=3 */
    return type == 2 && mach == 3;
}

static void resolve_path(const char *name, char *out, int outmax)
{
    char cwd[96];
    int n;

    if (!name || !out || outmax < 2)
        return;
    if (name[0] == '/') {
        strncpy_u(out, name, outmax);
        return;
    }
    sys_getcwd(cwd, sizeof(cwd));
    n = strlen_u(cwd);
    if (n <= 0) {
        strncpy_u(out, name, outmax);
        return;
    }
    strncpy_u(out, cwd, outmax);
    if (n + 1 < outmax && (n == 0 || cwd[n - 1] != '/')) {
        out[n++] = '/';
        out[n] = 0;
    }
    strncpy_u(out + n, name, outmax - n);
}

int main(void)
{
    char line[160];
    char *argv[8];
    char path[128];
    char args[128];
    int argc, i, pos, afd, pid, st = 0;

    read_argv(line, sizeof(line));
    argc = split_args(line, argv, 8);
    if (argc < 1) {
        println("usage: run FILE [args...]");
        return 1;
    }

    resolve_path(argv[0], path, sizeof(path));
    if (!sys_exists(path)) {
        print("run: not found: ");
        println(path);
        return 1;
    }
    if (!is_app(path)) {
        print("run: not an application: ");
        println(path);
        return 1;
    }

    args[0] = 0;
    pos = 0;
    for (i = 1; i < argc; i++) {
        int L = strlen_u(argv[i]);
        if (pos + L + 2 >= (int)sizeof(args))
            break;
        if (pos)
            args[pos++] = ' ';
        memcpy_u(args + pos, argv[i], L);
        pos += L;
        args[pos] = 0;
    }
    afd = sys_open("/sys/run/argv", O_WRITE | O_CREATE | O_TRUNC);
    if (afd >= 0) {
        sys_write(afd, args, strlen_u(args));
        sys_close(afd);
    }

    pid = sys_spawn(path);
    if (pid < 0) {
        println("run: spawn failed");
        return 1;
    }
    {
        int child = pid;
        do {
            st = 0;
            pid = sys_wait(child, &st);
            if (pid == -2 && sys_cons_hosted())
                sys_yield();
        } while (pid == -2);
    }
    return st >= 0 ? st : 0;
}
