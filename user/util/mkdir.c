#include "libmp.h"

int main(void)
{
    char line[128];
    char *argv[8];
    char cwd[128];
    int argc, i, rc = 0;
    read_argv(line, sizeof(line));
    argc = split_args(line, argv, 8);
    if (argc < 1) {
        println("usage: mkdir DIR...");
        return 1;
    }
    sys_getcwd(cwd, sizeof(cwd));
    for (i = 0; i < argc; i++) {
        if (sys_mkdir(argv[i]) < 0) {
            println("mkdir: fail");
            rc = 1;
        } else if (cwd[0] == '/' && cwd[1] == 0)
            println("(note: / is mount table — use /disk/... to keep after reboot)");
    }
    return rc;
}
