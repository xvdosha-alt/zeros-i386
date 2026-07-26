#include "libmp.h"

#define O_READ 1

static void reply_end(int c)
{
    sys_send(c, ".\n", 2);
}

static void reply_str(int c, const char *s)
{
    sys_send(c, s, strlen_u(s));
}

static void cmd_ls(int c, const char *path)
{
    char buf[512];
    if (!path || !path[0])
        path = "/";
    if (sys_listdir(path, buf, sizeof(buf)) < 0)
        reply_str(c, "ls: fail\n");
    else
        reply_str(c, buf);
}

static void cmd_cat(int c, const char *path)
{
    char buf[512];
    int fd, n;
    if (!path || !path[0]) {
        reply_str(c, "usage: cat PATH\n");
        return;
    }
    fd = sys_open(path, O_READ);
    if (fd < 0) {
        reply_str(c, "cat: open fail\n");
        return;
    }
    while ((n = sys_read(fd, buf, sizeof(buf))) > 0)
        sys_send(c, buf, n);
    sys_close(fd);
    reply_str(c, "\n");
}

static void handle_line(int c, char *line)
{
    char *argv[8];
    int argc = split_args(line, argv, 8);
    if (argc < 1)
        return;
    if (!strcmp_u(argv[0], "help")) {
        reply_str(c, "zerOS sshd — cleartext shell\n");
        reply_str(c, "cmds: help pwd ls cat echo uname ifconfig exit\n");
    } else if (!strcmp_u(argv[0], "pwd")) {
        char cwd[128];
        sys_getcwd(cwd, sizeof(cwd));
        reply_str(c, cwd);
        reply_str(c, "\n");
    } else if (!strcmp_u(argv[0], "ls")) {
        cmd_ls(c, argc > 1 ? argv[1] : "/");
    } else if (!strcmp_u(argv[0], "cat") && argc > 1) {
        cmd_cat(c, argv[1]);
    } else if (!strcmp_u(argv[0], "echo")) {
        int i;
        for (i = 1; i < argc; i++) {
            if (i > 1)
                reply_str(c, " ");
            reply_str(c, argv[i]);
        }
        reply_str(c, "\n");
    } else if (!strcmp_u(argv[0], "uname")) {
        reply_str(c, "zerOS i386 cleartext-sshd\n");
    } else if (!strcmp_u(argv[0], "ifconfig")) {
        char buf[160];
        sys_ifconfig(buf, sizeof(buf));
        reply_str(c, buf);
    } else if (!strcmp_u(argv[0], "exit") || !strcmp_u(argv[0], "logout") ||
               !strcmp_u(argv[0], "quit")) {
        reply_str(c, "bye\n");
        reply_end(c);
        return;
    } else {
        reply_str(c, "unknown: ");
        reply_str(c, argv[0]);
        reply_str(c, "\n");
    }
    reply_end(c);
}

static int read_line(int c, char *line, int max)
{
    int len = 0, n;
    char buf[64];
    while (len + 1 < max) {
        n = sys_recv(c, buf, sizeof(buf));
        if (n <= 0)
            return -1;
        {
            int i;
            for (i = 0; i < n; i++) {
                if (buf[i] == '\n' || buf[i] == '\r') {
                    line[len] = 0;
                    return len;
                }
                if (buf[i] >= 32 && buf[i] < 127)
                    line[len++] = buf[i];
            }
        }
    }
    line[len] = 0;
    return len;
}

int main(void)
{
    int s, c;
    char line[180], user[32];
    println("sshd listening :22 (cleartext, not OpenSSH)");
    s = sys_socket(2);
    if (s < 0 || sys_bind(s, 0, 22) < 0 || sys_listen(s, 1) < 0) {
        println("sshd: bind/listen fail");
        return 1;
    }
    for (;;) {
        c = sys_accept(s);
        if (c < 0)
            continue;
        println("sshd: session open");
        strncpy_u(user, "root", sizeof(user));
        if (read_line(c, line, sizeof(line)) < 0) {
            sys_close(c);
            continue;
        }
        if (!strncmp_u(line, "USER ", 5))
            strncpy_u(user, line + 5, sizeof(user));
        reply_str(c, "zerOS sshd 0.1 — welcome ");
        reply_str(c, user);
        reply_str(c, "\n");
        reply_str(c, "type help; not OpenSSH\n");
        reply_end(c);
        for (;;) {
            if (read_line(c, line, sizeof(line)) < 0)
                break;
            if (!strcmp_u(line, "exit") || !strcmp_u(line, "logout") || !strcmp_u(line, "quit")) {
                reply_str(c, "bye\n");
                reply_end(c);
                break;
            }
            handle_line(c, line);
        }
        sys_close(c);
        println("sshd: session closed");
        
        break;
    }
    sys_close(s);
    return 0;
}
