#include "libmp.h"

static int path_is_dir(const char *path)
{
    char buf[4];
    return sys_listdir(path, buf, sizeof(buf)) >= 0;
}

static void join_path(char *out, int outn, const char *dir, const char *name)
{
    int n;
    if (!strcmp_u(dir, "/")) {
        out[0] = '/';
        strncpy_u(out + 1, name, outn - 1);
        return;
    }
    strncpy_u(out, dir, outn);
    n = strlen_u(out);
    if (n + 1 < outn)
        out[n++] = '/';
    strncpy_u(out + n, name, outn - n);
}

static void parent_path(char *out, int outn, const char *path)
{
    int i, slash = -1;
    if (!path || !path[0] || !strcmp_u(path, "/")) {
        strncpy_u(out, "/", outn);
        return;
    }
    for (i = 0; path[i]; i++)
        if (path[i] == '/')
            slash = i;
    if (slash <= 0) {
        strncpy_u(out, "/", outn);
        return;
    }
    if (slash + 1 > outn)
        slash = outn - 1;
    for (i = 0; i < slash; i++)
        out[i] = path[i];
    out[slash] = 0;
}

/* Resolve path to absolute without kernel normalize for ".." beyond getcwd. */
static void make_abs(char *out, int outn, const char *path)
{
    char cwd[128];
    sys_getcwd(cwd, sizeof(cwd));
    if (!path || !path[0] || !strcmp_u(path, ".")) {
        strncpy_u(out, cwd, outn);
        return;
    }
    if (!strcmp_u(path, "..")) {
        parent_path(out, outn, cwd);
        return;
    }
    if (path[0] == '/') {
        strncpy_u(out, path, outn);
        return;
    }
    join_path(out, outn, cwd, path);
}

static int is_protected(const char *abs)
{
    return !strcmp_u(abs, "/") ||
           !strcmp_u(abs, "/sys") ||
           !strcmp_u(abs, "/disk");
}

static int rm_one(const char *path, int recursive)
{
    char abs[160];
    make_abs(abs, sizeof(abs), path);
    if (is_protected(abs)) {
        println("rm: refusing to remove protected path");
        return -1;
    }
    if (path_is_dir(abs)) {
        if (!recursive) {
            println("rm: is a directory");
            return -1;
        }
        {
            char buf[512];
            char name[48];
            char full[160];
            int p = 0;
            if (sys_listdir(abs, buf, sizeof(buf)) < 0) {
                println("rm: fail");
                return -1;
            }
            while (buf[p]) {
                int nn = 0;
                while (buf[p] && buf[p] != '\n' && nn + 1 < (int)sizeof(name))
                    name[nn++] = buf[p++];
                name[nn] = 0;
                if (buf[p] == '\n')
                    p++;
                if (!name[0] || !strcmp_u(name, ".") || !strcmp_u(name, ".."))
                    continue;
                join_path(full, sizeof(full), abs, name);
                if (is_protected(full)) {
                    println("rm: refusing to remove protected path");
                    return -1;
                }
                if (rm_one(full, 1) < 0)
                    return -1;
            }
        }
    }
    if (sys_unlink(abs) < 0) {
        println("rm: fail");
        return -1;
    }
    return 0;
}

int main(void)
{
    char line[128];
    char *argv[16];
    int argc, i, recursive = 0, rc = 0;
    read_argv(line, sizeof(line));
    argc = split_args(line, argv, 16);
    if (argc < 1) {
        println("usage: rm [-r] FILE...");
        return 1;
    }
    for (i = 0; i < argc; i++) {
        if (argv[i][0] == '-') {
            int j;
            for (j = 1; argv[i][j]; j++) {
                if (argv[i][j] == 'r' || argv[i][j] == 'R' || argv[i][j] == 'f')
                    recursive = 1;
            }
            continue;
        }
        if (rm_one(argv[i], recursive) < 0)
            rc = 1;
    }
    return rc;
}
