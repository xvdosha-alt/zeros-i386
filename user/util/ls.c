#include "libmp.h"

int main(void)
{
    char line[128];
    char *argv[8];
    char buf[2048];
    const char *path = ".";
    int argc, i, longfmt = 0;

    read_argv(line, sizeof(line));
    argc = split_args(line, argv, 8);
    for (i = 0; i < argc; i++) {
        if (argv[i][0] == '-') {
            int j;
            for (j = 1; argv[i][j]; j++)
                if (argv[i][j] == 'l')
                    longfmt = 1;
        } else
            path = argv[i];
    }
    if (sys_listdir(path, buf, sizeof(buf)) < 0) {
        println("ls: fail");
        return 1;
    }
    if (!longfmt) {
        print(buf);
        return 0;
    }
    {
        int p = 0;
        while (buf[p]) {
            char name[48];
            char full[160];
            char probe[4];
            int n = 0, dn;
            while (buf[p] && buf[p] != '\n' && n + 1 < (int)sizeof(name))
                name[n++] = buf[p++];
            name[n] = 0;
            if (buf[p] == '\n')
                p++;
            if (!name[0])
                continue;
            if (!strcmp_u(path, "/")) {
                full[0] = '/';
                strncpy_u(full + 1, name, (int)sizeof(full) - 1);
            } else {
                strncpy_u(full, path, sizeof(full));
                dn = strlen_u(full);
                if (dn + 1 < (int)sizeof(full))
                    full[dn++] = '/';
                strncpy_u(full + dn, name, (int)sizeof(full) - dn);
            }
            if (sys_listdir(full, probe, sizeof(probe)) >= 0)
                print("d ");
            else
                print("- ");
            println(name);
        }
    }
    return 0;
}
