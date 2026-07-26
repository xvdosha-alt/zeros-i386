#include "libmp.h"

#define O_READ 1

int main(void)
{
    int s, c, n;
    char req[512];
    char resp[1024];
    char body[512];
    int fd;
    println("httpd listening :8080");
    s = sys_socket(2);
    if (s < 0) {
        println("socket fail");
        return 1;
    }
    if (sys_bind(s, 0, 8080) < 0) {
        println("bind fail");
        return 1;
    }
    sys_listen(s, 1);
    for (;;) {
        c = sys_accept(s);
        if (c < 0)
            continue;
        n = sys_recv(c, req, sizeof(req) - 1);
        if (n < 0)
            n = 0;
        req[n] = 0;
        body[0] = 0;
        fd = sys_open("/sys/www/index.html", O_READ);
        if (fd >= 0) {
            n = sys_read(fd, body, sizeof(body) - 1);
            sys_close(fd);
            if (n < 0)
                n = 0;
            body[n] = 0;
        } else {
            strncpy_u(body, "zerOS httpd\n", sizeof(body));
        }
        strncpy_u(resp, "HTTP/1.0 200 OK\r\nContent-Type: text/html\r\n\r\n", sizeof(resp));
        sys_send(c, resp, strlen_u(resp));
        sys_send(c, body, strlen_u(body));
    }
    return 0;
}
