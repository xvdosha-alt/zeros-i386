#include "libmp.h"

#define O_WRITE 2
#define O_TRUNC 8
#define O_CREATE 16
#define O_APPEND 4

int main(void)
{
    char line[256];
    char *argv[16];
    char out[256];
    int argc, i, pos = 0, redir = 0, append = 0;
    const char *file = 0;

    read_argv(line, sizeof(line));
    argc = split_args(line, argv, 16);
    for (i = 0; i < argc; i++) {
        if (!strcmp_u(argv[i], ">") || !strcmp_u(argv[i], ">>")) {
            redir = 1;
            append = argv[i][1] == '>';
            if (i + 1 < argc)
                file = argv[i + 1];
            break;
        }
        if (pos && pos + 1 < (int)sizeof(out))
            out[pos++] = ' ';
        {
            int L = strlen_u(argv[i]);
            if (pos + L >= (int)sizeof(out))
                break;
            memcpy_u(out + pos, argv[i], L);
            pos += L;
        }
    }
    out[pos] = 0;
    if (redir && file) {
        int fd = sys_open(file, O_WRITE | O_CREATE | (append ? O_APPEND : O_TRUNC));
        if (fd < 0) {
            println("echo: redirect fail");
            return 1;
        }
        sys_write(fd, out, strlen_u(out));
        sys_write(fd, "\n", 1);
        sys_close(fd);
        return 0;
    }
    println(out);
    return 0;
}
