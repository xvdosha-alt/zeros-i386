#include "libmp.h"

static void pump_recv(int s)
{
    char buf[512];
    int n;
    for (;;) {
        n = sys_recv(s, buf, sizeof(buf));
        if (n <= 0)
            break;
        sys_write(1, buf, n);
    }
}

static void interactive(int s)
{
    char line[200];
    int len = 0, key;
    println("(nc) connected — type lines, empty line / Ctrl+C to quit");
    for (;;) {
        
        {
            char buf[256];
            int n = sys_recv(s, buf, sizeof(buf));
            if (n > 0)
                sys_write(1, buf, n);
        }
        key = 0;
        if (sys_read(0, &key, 4) != 4)
            continue;
        if (key == 3) {
            println("^C");
            break;
        }
        if (key == '\n' || key == '\r') {
            sys_write(1, "\n", 1);
            line[len] = 0;
            if (len == 0)
                break;
            line[len++] = '\n';
            sys_send(s, line, len);
            len = 0;
            pump_recv(s);
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
            line[len++] = (char)key;
            {
                char c = (char)key;
                sys_write(1, &c, 1);
            }
        }
    }
}

int main(void)
{
    char argvbuf[128], *argv[8];
    int argc, listen = 0, s, c;
    uint16_t port = 0;
    char *host = 0;

    read_argv(argvbuf, sizeof(argvbuf));
    argc = split_args(argvbuf, argv, 8);
    {
        int i;
        for (i = 0; i < argc; i++) {
            if (!strcmp_u(argv[i], "-l"))
                listen = 1;
            else if (!strcmp_u(argv[i], "-p") && i + 1 < argc)
                port = (uint16_t)atoi_u(argv[++i]);
            else if (argv[i][0] != '-') {
                if (!host)
                    host = argv[i];
                else if (!port)
                    port = (uint16_t)atoi_u(argv[i]);
            }
        }
    }

    if (listen) {
        if (!port) {
            println("usage: nc -l -p PORT");
            return 1;
        }
        s = sys_socket(2);
        if (s < 0 || sys_bind(s, 0, port) < 0 || sys_listen(s, 1) < 0) {
            println("nc: listen fail");
            return 1;
        }
        print("nc: listening :");
        print_uint(port);
        sys_write(1, "\n", 1);
        for (;;) {
            c = sys_accept(s);
            if (c < 0)
                continue;
            println("nc: client connected");
            interactive(c);
            sys_close(c);
            break;
        }
        sys_close(s);
        return 0;
    }

    if (!host || !port) {
        println("usage: nc HOST PORT");
        println("       nc -l -p PORT");
        return 1;
    }
    s = tcp_connect_host(host, port);
    if (s < 0) {
        println("nc: connect fail");
        return 1;
    }
    interactive(s);
    sys_close(s);
    return 0;
}
