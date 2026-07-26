#include "libmp.h"

#define O_READ 1
#define O_WRITE 2
#define O_TRUNC 8
#define O_CREATE 16

static int copy_file(const char *src, const char *dst)
{
    char buf[256];
    int in, out, n;
    in = sys_open(src, O_READ);
    if (in < 0)
        return -1;
    out = sys_open(dst, O_WRITE | O_CREATE | O_TRUNC);
    if (out < 0) {
        sys_close(in);
        return -1;
    }
    for (;;) {
        n = sys_read(in, buf, sizeof(buf));
        if (n <= 0)
            break;
        if (sys_write(out, buf, n) != n) {
            sys_close(in);
            sys_close(out);
            return -1;
        }
    }
    sys_close(in);
    sys_close(out);
    return 0;
}

int main(void)
{
    char line[128];
    char *argv[8];
    int argc;
    read_argv(line, sizeof(line));
    argc = split_args(line, argv, 8);
    if (argc < 2) {
        println("usage: mv SRC DST");
        return 1;
    }
    if (copy_file(argv[0], argv[1]) < 0 || sys_unlink(argv[0]) < 0) {
        println("mv: fail");
        return 1;
    }
    return 0;
}
