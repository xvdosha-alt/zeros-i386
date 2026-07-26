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

static void tree_walk(const char *path, int depth)
{
    char buf[512];
    char name[48];
    char full[160];
    int p = 0, i;
    if (depth > 4)
        return;
    if (sys_listdir(path, buf, sizeof(buf)) < 0)
        return;
    while (buf[p]) {
        int n = 0;
        while (buf[p] && buf[p] != '\n' && n + 1 < (int)sizeof(name))
            name[n++] = buf[p++];
        name[n] = 0;
        if (buf[p] == '\n')
            p++;
        if (!name[0] || !strcmp_u(name, ".") || !strcmp_u(name, ".."))
            continue;
        for (i = 0; i < depth; i++)
            print("  ");
        print("|- ");
        println(name);
        join_path(full, sizeof(full), path, name);
        if (path_is_dir(full))
            tree_walk(full, depth + 1);
    }
}

int main(void)
{
    char line[128];
    char *argv[4];
    const char *path = ".";
    int argc;
    read_argv(line, sizeof(line));
    argc = split_args(line, argv, 4);
    if (argc >= 1)
        path = argv[0];
    println(path);
    tree_walk(path, 0);
    return 0;
}
