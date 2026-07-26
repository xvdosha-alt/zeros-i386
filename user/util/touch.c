#include "libmp.h"

#define O_WRITE 2
#define O_CREATE 16

int main(void)
{
    char line[128];
    char *argv[8];
    int argc, i, fd, rc = 0;
    read_argv(line, sizeof(line));
    argc = split_args(line, argv, 8);
    if (argc < 1) {
        println("usage: touch FILE...");
        return 1;
    }
    for (i = 0; i < argc; i++) {
        fd = sys_open(argv[i], O_WRITE | O_CREATE);
        if (fd < 0) {
            println("touch: fail");
            rc = 1;
        } else
            sys_close(fd);
    }
    return rc;
}
