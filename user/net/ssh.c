#include "libmp.h"

static void usage(void)
{
    println("usage: ssh [user@]HOST [PORT]");
    println("zerOS cleartext rsh to guest sshd (default :22)");
    println("not OpenSSH — no TLS/keys");
}

static void parse_target(char *spec, char *user, int usz, char *host, int hsz)
{
    char *at = 0;
    int i;
    strncpy_u(user, "root", usz);
    for (i = 0; spec[i]; i++) {
        if (spec[i] == '@')
            at = &spec[i];
    }
    if (at) {
        *at = 0;
        strncpy_u(user, spec, usz);
        strncpy_u(host, at + 1, hsz);
    } else {
        strncpy_u(host, spec, hsz);
    }
}

static int session_recv(int s)
{
    char buf[512];
    int n, i;
    int st = 0; 
    for (;;) {
        n = sys_recv(s, buf, sizeof(buf));
        if (n <= 0)
            return -1;
        for (i = 0; i < n; i++) {
            char c = buf[i];
            if (st == 0) {
                if (c == '\n')
                    st = 1;
                sys_write(1, &c, 1);
            } else if (st == 1) {
                if (c == '.') {
                    st = 2;
                } else {
                    st = (c == '\n') ? 1 : 0;
                    sys_write(1, &c, 1);
                }
            } else {
                if (c == '\n' || c == '\r')
                    return 0;
                sys_write(1, ".", 1);
                sys_write(1, &c, 1);
                st = (c == '\n') ? 1 : 0;
            }
        }
    }
}

int main(void)
{
    char argvbuf[128], *argv[8], user[32], host[64], line[180], hello[128];
    int argc, s, len, key;
    uint16_t port = 22;

    read_argv(argvbuf, sizeof(argvbuf));
    argc = split_args(argvbuf, argv, 8);
    if (argc < 1) {
        usage();
        return 1;
    }
    parse_target(argv[0], user, sizeof(user), host, sizeof(host));
    if (argc >= 2)
        port = (uint16_t)atoi_u(argv[1]);

    print("ssh: ");
    print(user);
    sys_write(1, "@", 1);
    print(host);
    sys_write(1, ":", 1);
    print_uint(port);
    println(" (cleartext zerOS)");

    s = tcp_connect_host(host, port);
    if (s < 0) {
        println("ssh: connect fail (is sshd running?)");
        return 1;
    }

    strncpy_u(hello, "USER ", sizeof(hello));
    strncpy_u(hello + strlen_u(hello), user, (int)sizeof(hello) - strlen_u(hello));
    strncpy_u(hello + strlen_u(hello), "\n", (int)sizeof(hello) - strlen_u(hello));
    sys_send(s, hello, strlen_u(hello));
    session_recv(s);

    print(user);
    print("@");
    print(host);
    print("> ");
    len = 0;
    for (;;) {
        key = 0;
        if (sys_read(0, &key, 4) != 4)
            continue;
        if (key == 3) {
            println("^C");
            break;
        }
        if (key == 4 && len == 0)
            break;
        if (key == '\n' || key == '\r') {
            sys_write(1, "\n", 1);
            line[len] = 0;
            if (!strcmp_u(line, "exit") || !strcmp_u(line, "logout") || !strcmp_u(line, "quit"))
                break;
            line[len++] = '\n';
            sys_send(s, line, len);
            session_recv(s);
            len = 0;
            print(user);
            print("@");
            print(host);
            print("> ");
            continue;
        }
        if (key == '\b' || key == 127) {
            if (len > 0) {
                len--;
                sys_write(1, "\b \b", 3);
            }
            continue;
        }
        if (key >= 32 && key < 127 && len + 2 < (int)sizeof(line)) {
            char c = (char)key;
            line[len++] = c;
            sys_write(1, &c, 1);
        }
    }
    sys_send(s, "exit\n", 5);
    sys_close(s);
    println("Connection closed.");
    return 0;
}
