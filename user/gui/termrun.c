#include "termrun.h"
#include "libmp.h"

#define O_WRITE 2
#define O_CREATE 16
#define O_TRUNC 8

static int flush_cons(int pid, TermOutFn out, void *ctx, char *acc, int *accn,
                      int accmax, int *caret)
{
    char buf[256];
    int n, i, changed = 0;
    for (;;) {
        n = sys_cons_read(pid, buf, (int)sizeof(buf) - 1);
        if (n <= 0)
            break;
        changed = 1;
        for (i = 0; i < n; i++) {
            if ((unsigned char)buf[i] == 0x06) {
                if (i + 1 < n) {
                    if (caret)
                        *caret = (unsigned char)buf[i + 1];
                    i++;
                }
                continue;
            }
            if (buf[i] == '\r') {
                *accn = 0;
                if (caret)
                    *caret = 0;
                continue;
            }
            if (buf[i] == '\b' || buf[i] == 127) {
                if (*accn > 0)
                    (*accn)--;
                continue;
            }
            if (buf[i] == '\n') {
                acc[*accn] = 0;
                if (out)
                    out(ctx, acc);
                *accn = 0;
                if (caret)
                    *caret = 0;
            } else if (*accn + 1 < accmax) {
                acc[(*accn)++] = buf[i];
            }
        }
    }
    return changed;
}

int term_run_cmd(const char *cmdline, TermOutFn out, void *ctx)
{
    char work[160];
    char path[128];
    char args[128];
    char acc[96];
    char *argv[8];
    int argc, i, pos, pid, st, afd, accn;
    const char *cmd;

    if (!cmdline || !cmdline[0])
        return 0;
    strncpy_u(work, cmdline, sizeof(work));
    argc = split_args(work, argv, 8);
    if (argc < 1)
        return 0;
    cmd = argv[0];

    if (!strcmp_u(cmd, "help")) {
        if (out) {
            out(ctx, "cmds: any /sys/bin app  |  msh = full shell (DOS box)");
            out(ctx, "host: cd clear help exit");
        }
        return 0;
    }
    if (!strcmp_u(cmd, "clear"))
        return 2;
    if (!strcmp_u(cmd, "exit"))
        return 3;
    if (!strcmp_u(cmd, "pwd")) {
        char cwd[96];
        sys_getcwd(cwd, sizeof(cwd));
        if (out)
            out(ctx, cwd);
        return 0;
    }
    if (!strcmp_u(cmd, "cd")) {
        const char *t = argc > 1 ? argv[1] : "/sys";
        if (sys_chdir(t) != 0 && out)
            out(ctx, "cd: fail");
        return 0;
    }
    /* "msh" alone → caller should start a TermSession instead */
    if (!strcmp_u(cmd, "msh") && argc == 1)
        return 4;

    if (cmd[0] == '/')
        strncpy_u(path, cmd, sizeof(path));
    else {
        strncpy_u(path, "/sys/bin/", sizeof(path));
        strncpy_u(path + 9, cmd, (int)sizeof(path) - 9);
    }
    if (!sys_exists(path)) {
        if (out)
            out(ctx, "not found");
        return 1;
    }

    args[0] = 0;
    pos = 0;
    for (i = 1; i < argc; i++) {
        int L = strlen_u(argv[i]);
        if (pos + L + 2 >= (int)sizeof(args))
            break;
        if (pos)
            args[pos++] = ' ';
        memcpy_u(args + pos, argv[i], L);
        pos += L;
        args[pos] = 0;
    }
    afd = sys_open("/sys/run/argv", O_WRITE | O_CREATE | O_TRUNC);
    if (afd >= 0) {
        sys_write(afd, args, strlen_u(args));
        sys_close(afd);
    }

    sys_cons_attach(1);
    pid = sys_spawn(path);
    if (pid < 0) {
        if (out)
            out(ctx, "spawn failed");
        return 1;
    }

    accn = 0;
    {
        int child = pid;
        for (;;) {
            int r = sys_wait(child, &st);
            flush_cons(child, out, ctx, acc, &accn, (int)sizeof(acc), 0);
            if (r == -2)
                continue;
            if (r < 0) {
                if (out)
                    out(ctx, "wait fail");
                return 1;
            }
            break;
        }
    }
    if (accn > 0) {
        acc[accn] = 0;
        if (out)
            out(ctx, acc);
    }
    return st >= 0 ? st : 0;
}

int term_session_start(TermSession *s, const char *path)
{
    if (!s)
        return -1;
    s->pid = -1;
    s->accn = 0;
    s->acc[0] = 0;
    s->caret = 0;
    s->alive = 0;
    s->painted = 0;
    if (!path)
        path = "/sys/bin/msh";
    if (!sys_exists(path))
        return -1;
    sys_cons_attach(1);
    s->pid = sys_spawn(path);
    if (s->pid < 0)
        return -1;
    s->alive = 1;
    return 0;
}

int term_session_pump(TermSession *s, int key, TermOutFn out, void *ctx)
{
    int r, st = 0;
    int changed;
    int pid;
    if (!s || !s->alive || s->pid < 0)
        return 0;
    pid = s->pid;
    s->painted = 0;
    if (key >= 0) {
        sys_cons_putkey(pid, key);
        {
            int gid = sys_gui_find(pid);
            if (gid >= 0)
                sys_gui_post(gid, INP_KEY, key);
        }
    }
    r = sys_wait(pid, &st);
    changed = flush_cons(pid, out, ctx, s->acc, &s->accn, (int)sizeof(s->acc),
                         &s->caret);
    s->painted = changed ? 1 : 0;
    if (r == -2 || st == -2 || r < 0) {
        s->pid = pid;
        s->alive = 1;
        return 1;
    }
    /* r > 0: child exited and was reaped */
    if (s->accn > 0) {
        s->acc[s->accn] = 0;
        if (out)
            out(ctx, s->acc);
        s->accn = 0;
        s->painted = 1;
    }
    s->alive = 0;
    s->pid = -1;
    return 0;
}

const char *term_session_live(TermSession *s)
{
    if (!s || s->accn <= 0)
        return "";
    s->acc[s->accn] = 0;
    return s->acc;
}

int term_session_caret(const TermSession *s)
{
    if (!s)
        return 0;
    if (s->caret < 0 || s->caret > s->accn)
        return s->accn;
    return s->caret;
}

void term_session_stop(TermSession *s)
{
    int pid, st, r;
    if (!s)
        return;
    pid = s->pid;
    s->alive = 0;
    s->pid = -1;
    s->accn = 0;
    s->caret = 0;
    s->painted = 0;
    if (pid < 0)
        return;
    /* Kill the hosted process (and its children), then reap. */
    sys_kill(pid);
    do {
        st = 0;
        r = sys_wait(pid, &st);
    } while (r == -2);
    /* Sweep any other zombie children of this host. */
    for (;;) {
        st = 0;
        r = sys_wait(-1, &st);
        if (r <= 0)
            break;
    }
}
