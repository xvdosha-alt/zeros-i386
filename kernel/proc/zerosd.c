#include "zerosd.h"
#include "vfs.h"
#include "proc.h"
#include "tty.h"
#include "string.h"
#include "net.h"

typedef struct {
    char name[32];
    char after[32];
    char exec[64];
    int type_oneshot;
    int restart;
    int started;
    int pid;
} Unit;

static Unit units[16];
static int nunits;

static char *trim(char *s)
{
    char *e;
    while (*s == ' ' || *s == '\t' || *s == '\r')
        s++;
    e = s + kstrlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) {
        e--;
        *e = 0;
    }
    return s;
}

static void parse_unit_text(const char *text, Unit *u)
{
    char line[128];
    size_t i = 0, j;
    kmemset(u, 0, sizeof(*u));
    while (text[i]) {
        j = 0;
        while (text[i] && text[i] != '\n' && j + 1 < sizeof(line))
            line[j++] = text[i++];
        line[j] = 0;
        if (text[i] == '\n')
            i++;
        {
            char *p = trim(line);
            if (!p[0] || p[0] == '[')
                continue;
            if (!kstrncmp(p, "Name=", 5))
                kstrncpy(u->name, trim(p + 5), sizeof(u->name));
            else if (!kstrncmp(p, "After=", 6))
                kstrncpy(u->after, trim(p + 6), sizeof(u->after));
            else if (!kstrncmp(p, "Exec=", 5))
                kstrncpy(u->exec, trim(p + 5), sizeof(u->exec));
            else if (!kstrncmp(p, "Type=", 5))
                u->type_oneshot = kstrcmp(trim(p + 5), "oneshot") == 0;
            else if (!kstrncmp(p, "Restart=", 8))
                u->restart = kstrcmp(trim(p + 8), "on-failure") == 0;
        }
    }
}

static void load_units(void)
{
    char list[512];
    char name[64];
    char path[96];
    char body[1024];
    int n, i, j, k;
    nunits = 0;
    n = vfs_listdir("/sys/etc/zerosd", list, sizeof(list));
    if (n < 0)
        return;
    i = 0;
    while (list[i] && nunits < 16) {
        j = 0;
        while (list[i] && list[i] != '\n' && j + 1 < (int)sizeof(name))
            name[j++] = list[i++];
        name[j] = 0;
        if (list[i] == '\n')
            i++;
        if (!name[0])
            continue;
        k = (int)kstrlen(name);
        if (k < 5 || kstrcmp(name + k - 5, ".unit") != 0)
            continue;
        kstrncpy(path, "/sys/etc/zerosd/", sizeof(path));
        kstrncpy(path + 16, name, sizeof(path) - 16);
        n = vfs_read_file(path, body, sizeof(body) - 1);
        if (n < 0)
            continue;
        body[n] = 0;
        parse_unit_text(body, &units[nunits]);
        if (!units[nunits].name[0])
            kstrncpy(units[nunits].name, name, sizeof(units[nunits].name));
        nunits++;
    }
}

static Unit *find_unit(const char *name)
{
    int i;
    for (i = 0; i < nunits; i++)
        if (!kstrcmp(units[i].name, name))
            return &units[i];
    return 0;
}

static int deps_ready(Unit *u)
{
    Unit *dep;
    if (!u->after[0])
        return 1;
    dep = find_unit(u->after);
    if (!dep)
        return 1;
    return dep->started;
}

static void start_unit(Unit *u)
{
    int pid;
    if (u->started && !u->type_oneshot)
        return;
    if (!kstrcmp(u->exec, "kernel:netd") || !kstrcmp(u->name, "netd")) {
        net_start_dhcp();
        u->started = 1;
        tty_write("[zerosd] started ");
        tty_writeln(u->name);
        return;
    }
    if (!kstrcmp(u->exec, "kernel:local-fs") || !kstrcmp(u->name, "local-fs")) {
        u->started = 1;
        tty_writeln("[zerosd] started local-fs");
        return;
    }
    if (!u->exec[0]) {
        u->started = 1;
        return;
    }
    pid = proc_spawn_elf(u->exec, 0, 0);
    if (pid < 0) {
        tty_write("[zerosd] fail ");
        tty_writeln(u->name);
        u->started = 1;
        return;
    }
    u->pid = pid;
    if (u->type_oneshot) {
        proc_run_blocking(proc_get(pid));
        proc_reap(pid);
        u->started = 1;
    } else if (!kstrcmp(u->name, "msh") || !kstrcmp(u->name, "session")) {
        u->started = 1;
        tty_write("[zerosd] console ");
        tty_writeln(u->name);
        for (;;) {
            proc_run_blocking(proc_get(pid));
            /* Coop yield returns here with process still alive — do not reap. */
            {
                Proc *pp = proc_get(pid);
                if (pp && pp->state != PROC_ZOMBIE)
                    continue;
            }
            proc_reap(pid);
            if (!u->restart)
                break;
            pid = proc_spawn_elf(u->exec, 0, 0);
            if (pid < 0)
                break;
            u->pid = pid;
        }
    } else {
        u->started = 1;
        tty_write("[zerosd] started ");
        tty_writeln(u->name);
    }
}

void zerosd_start(void)
{
    int progress, i;
    tty_writeln("zerOS zerosd 0.1");
    load_units();
    do {
        progress = 0;
        for (i = 0; i < nunits; i++) {
            if (units[i].started)
                continue;
            if (!deps_ready(&units[i]))
                continue;
            start_unit(&units[i]);
            progress = 1;
        }
    } while (progress);
    for (;;)
        __asm__ volatile ("sti; hlt");
}
