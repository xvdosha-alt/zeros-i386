#include "libmp.h"

#define O_READ 1

static int run_head_tail(int is_tail)
{
    char line[128];
    char *argv[8];
    char buf[512];
    const char *path;
    int argc, fd, n, lines = 10, i, count = 0;
    int starts[64];
    int nstarts = 0;

    read_argv(line, sizeof(line));
    argc = split_args(line, argv, 8);
    if (argc < 1) {
        println(is_tail ? "usage: tail [-n N] FILE" : "usage: head [-n N] FILE");
        return 1;
    }
    path = argv[argc - 1];
    for (i = 0; i + 1 < argc; i++) {
        if (!strcmp_u(argv[i], "-n") || !strcmp_u(argv[i], "-f"))
            lines = atoi_u(argv[i + 1]);
    }
    fd = sys_open(path, O_READ);
    if (fd < 0) {
        println("open fail");
        return 1;
    }
    n = sys_read(fd, buf, sizeof(buf) - 1);
    sys_close(fd);
    if (n < 0)
        return 1;
    buf[n] = 0;
    starts[nstarts++] = 0;
    for (i = 0; i < n; i++) {
        if (buf[i] == '\n' && i + 1 < n && nstarts < 64)
            starts[nstarts++] = i + 1;
    }
    if (is_tail) {
        int from = nstarts > lines ? nstarts - lines : 0;
        print(buf + starts[from]);
    } else {
        for (i = 0; i < n && count < lines; i++) {
            sys_write(1, &buf[i], 1);
            if (buf[i] == '\n')
                count++;
        }
    }
    return 0;
}

#ifdef IS_TAIL
int main(void) { return run_head_tail(1); }
#else
int main(void) { return run_head_tail(0); }
#endif
