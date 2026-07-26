#include "libmp.h"

int main(void)
{
    char argvbuf[128], *argv[8];
    int argc, s, len = 0, key;
    uint16_t port = 23;
    char line[200];
    char *host = 0;

    read_argv(argvbuf, sizeof(argvbuf));
    argc = split_args(argvbuf, argv, 8);
    if (argc < 1) {
        println("usage: telnet HOST [PORT]");
        return 1;
    }
    host = argv[0];
    if (argc >= 2)
        port = (uint16_t)atoi_u(argv[1]);

    print("Trying ");
    print(host);
    sys_write(1, "...\n", 4);
    s = tcp_connect_host(host, port);
    if (s < 0) {
        println("telnet: connect fail");
        return 1;
    }
    println("Connected.");
    for (;;) {
        {
            char buf[256];
            int n = sys_recv(s, buf, sizeof(buf));
            if (n > 0)
                sys_write(1, buf, n);
            else if (n < 0)
                break;
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
            line[len++] = '\n';
            sys_send(s, line, len);
            len = 0;
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
    sys_close(s);
    println("Connection closed.");
    return 0;
}
