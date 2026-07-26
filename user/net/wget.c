#include "libmp.h"

#define O_WRITE 2
#define O_CREATE 16
#define O_TRUNC 8

static int starts_with(const char *s, const char *p)
{
    return strncmp_u(s, p, strlen_u(p)) == 0;
}

static void basename_from_url(const char *url, char *out, int n)
{
    const char *p = url, *slash = url;
    while (*p) {
        if (*p == '/')
            slash = p + 1;
        p++;
    }
    if (!slash[0] || slash[0] == '?')
        strncpy_u(out, "index.html", n);
    else
        strncpy_u(out, slash, n);
}

static int parse_url(const char *url, char *host, int hsz, char *path, int psz, uint16_t *port)
{
    const char *p = url;
    int i = 0;
    *port = 80;
    if (starts_with(p, "https://")) {
        println("wget: no TLS — use http://");
        return -1;
    }
    if (starts_with(p, "http://"))
        p += 7;
    while (p[i] && p[i] != '/' && p[i] != ':' && i + 1 < hsz) {
        host[i] = p[i];
        i++;
    }
    host[i] = 0;
    if (!host[0])
        return -1;
    p += i;
    if (*p == ':') {
        p++;
        *port = (uint16_t)atoi_u(p);
        while (*p >= '0' && *p <= '9')
            p++;
    }
    if (*p == '/')
        strncpy_u(path, p, psz);
    else {
        path[0] = '/';
        path[1] = 0;
    }
    return 0;
}

int main(void)
{
    char argvbuf[256], *argv[8], host[64], path[128], dest[64], req[384], buf[1024];
    int argc, s, n, out, hdr_done = 0;
    uint16_t port = 80;
    char *url = 0;

    read_argv(argvbuf, sizeof(argvbuf));
    argc = split_args(argvbuf, argv, 8);
    if (argc < 1) {
        println("usage: wget URL");
        return 1;
    }
    url = argv[0];
    if (parse_url(url, host, sizeof(host), path, sizeof(path), &port) < 0)
        return 1;
    basename_from_url(url, dest, sizeof(dest));

    print("wget: ");
    print(host);
    print(" -> ");
    println(dest);

    s = tcp_connect_host(host, port);
    if (s < 0) {
        println("wget: connect fail");
        return 1;
    }
    strncpy_u(req, "GET ", sizeof(req));
    strncpy_u(req + strlen_u(req), path, (int)sizeof(req) - strlen_u(req));
    strncpy_u(req + strlen_u(req), " HTTP/1.0\r\nHost: ", (int)sizeof(req) - strlen_u(req));
    strncpy_u(req + strlen_u(req), host, (int)sizeof(req) - strlen_u(req));
    strncpy_u(req + strlen_u(req), "\r\nUser-Agent: wget/zerOS\r\nConnection: close\r\n\r\n",
              (int)sizeof(req) - strlen_u(req));
    sys_send(s, req, strlen_u(req));

    out = sys_open(dest, O_WRITE | O_CREATE | O_TRUNC);
    if (out < 0) {
        println("wget: open fail");
        sys_close(s);
        return 1;
    }
    for (;;) {
        n = sys_recv(s, buf, sizeof(buf));
        if (n <= 0)
            break;
        if (!hdr_done) {
            int j;
            for (j = 0; j + 3 < n; j++) {
                if (buf[j] == '\r' && buf[j + 1] == '\n' && buf[j + 2] == '\r' && buf[j + 3] == '\n') {
                    sys_write(out, buf + j + 4, n - j - 4);
                    hdr_done = 1;
                    break;
                }
            }
            continue;
        }
        sys_write(out, buf, n);
    }
    sys_close(out);
    sys_close(s);
    print("saved ");
    println(dest);
    return 0;
}
