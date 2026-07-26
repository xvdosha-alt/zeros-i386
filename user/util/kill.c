#include "libmp.h"

int main(void)
{
    char line[128];
    char *argv[8];
    int argc, pid, r;

    read_argv(line, sizeof(line));
    argc = split_args(line, argv, 8);
    if (argc < 1) {
        println("usage: kill <pid>");
        return 1;
    }
    pid = atoi_u(argv[0]);
    if (pid <= 1) {
        println("kill: refuse pid <= 1");
        return 1;
    }
    r = sys_kill(pid);
    if (r != 0) {
        println("kill: failed");
        return 1;
    }
    print("killed ");
    /* small pid echo */
    {
        char tmp[12];
        int i = 0, v = pid;
        if (v == 0)
            tmp[i++] = '0';
        while (v > 0 && i < 11) {
            tmp[i++] = (char)('0' + (v % 10));
            v /= 10;
        }
        while (i > 0)
            sys_write(1, &tmp[--i], 1);
    }
    println("");
    return 0;
}
