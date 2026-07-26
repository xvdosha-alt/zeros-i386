#include "libmp.h"

int main(void)
{
    char line[64];
    char *argv[4];
    char path[128];
    int argc;
    read_argv(line, sizeof(line));
    argc = split_args(line, argv, 4);
    if (argc < 1) {
        println("usage: which CMD");
        return 1;
    }
    strncpy_u(path, "/sys/bin/", sizeof(path));
    strncpy_u(path + 9, argv[0], (int)sizeof(path) - 9);
    if (sys_exists(path)) {
        println(path);
        return 0;
    }
    println("not found");
    return 1;
}
