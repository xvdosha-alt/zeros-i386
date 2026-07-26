#include "libmp.h"

#define O_WRITE 2
#define O_CREATE 16
#define O_TRUNC 8

static int starts_with(const char *s, const char *p)
{
    return strncmp_u(s, p, strlen_u(p)) == 0;
}

static int parse_url(const char *url, char *host, int hsz, char *path, int psz, uint16_t *port)
{
    const char *p = url;
    int i = 0;
    *port = 80;
    host[0] = 0;
    path[0] = '/';
    path[1] = 0;
    if (starts_with(p, "https://")) {
        println("curl: TLS not on zerOS (use http://)");
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
    return 0;
}

static void usage(void)
{
    println("usage: curl [opts] URL");
    println("  -I        HEAD only");
    println("  -v        verbose");
    println("  -o FILE   write body to FILE");
    println("  -d DATA   POST body");
    println("  -H HDR    add header (repeatable, max 4)");
}

int main(void)
{
    char argvbuf[256];
    char *argv[16];
    int argc, i, s, n, out = -1, head_only = 0, verbose = 0;
    char host[64], path[128], req[512], buf[1024];
    char method[8];
    char *url = 0, *outfile = 0, *post = 0;
    char *extra[4];
    int nextra = 0;
    uint16_t port = 80;
    int hdr_done = 0, status = 0;

    strncpy_u(method, "GET", sizeof(method));
    read_argv(argvbuf, sizeof(argvbuf));
    argc = split_args(argvbuf, argv, 16);
    for (i = 0; i < argc; i++) {
        if (!strcmp_u(argv[i], "-I"))
            head_only = 1;
        else if (!strcmp_u(argv[i], "-v"))
            verbose = 1;
        else if (!strcmp_u(argv[i], "-o") && i + 1 < argc)
            outfile = argv[++i];
        else if (!strcmp_u(argv[i], "-d") && i + 1 < argc) {
            post = argv[++i];
            strncpy_u(method, "POST", sizeof(method));
        } else if (!strcmp_u(argv[i], "-H") && i + 1 < argc && nextra < 4)
            extra[nextra++] = argv[++i];
        else if (argv[i][0] != '-')
            url = argv[i];
    }
    if (head_only)
        strncpy_u(method, "HEAD", sizeof(method));
    if (!url) {
        usage();
        return 1;
    }
    if (parse_url(url, host, sizeof(host), path, sizeof(path), &port) < 0)
        return 1;
    if (port == 0)
        port = 80;

    print("* connect ");
    print(host);
    {
        char pb[8];
        int v = (int)port, k = 0, n = 0;
        char rev[8];
        pb[n++] = ':';
        if (v <= 0)
            rev[k++] = '0';
        else {
            while (v > 0 && k < 7) {
                rev[k++] = (char)('0' + (v % 10));
                v /= 10;
            }
        }
        while (k > 0)
            pb[n++] = rev[--k];
        pb[n++] = '\n';
        sys_write(1, pb, n);
    }
    s = tcp_connect_host(host, port);
    if (s < 0) {
        println("curl: connect fail (timeout/ARP)");
        return 1;
    }
    println("* connected");

    
    {
        int pos = 0;
        char tmp[32];
        
        strncpy_u(req, method, sizeof(req));
        pos = strlen_u(req);
        req[pos++] = ' ';
        strncpy_u(req + pos, path, (int)sizeof(req) - pos);
        pos = strlen_u(req);
        strncpy_u(req + pos, " HTTP/1.0\r\nHost: ", (int)sizeof(req) - pos);
        pos = strlen_u(req);
        strncpy_u(req + pos, host, (int)sizeof(req) - pos);
        pos = strlen_u(req);
        if (port != 80) {
            req[pos++] = ':';
            {
                int v = port, k = 0;
                char rev[8];
                if (v == 0)
                    rev[k++] = '0';
                else {
                    while (v) {
                        rev[k++] = (char)('0' + v % 10);
                        v /= 10;
                    }
                }
                while (k && pos + 1 < (int)sizeof(req))
                    req[pos++] = rev[--k];
                req[pos] = 0;
            }
        }
        strncpy_u(req + pos, "\r\nUser-Agent: curl/zerOS\r\nConnection: close\r\n",
                  (int)sizeof(req) - pos);
        for (i = 0; i < nextra; i++) {
            pos = strlen_u(req);
            strncpy_u(req + pos, extra[i], (int)sizeof(req) - pos);
            pos = strlen_u(req);
            if (pos + 2 < (int)sizeof(req)) {
                req[pos++] = '\r';
                req[pos++] = '\n';
                req[pos] = 0;
            }
        }
        if (post) {
            pos = strlen_u(req);
            strncpy_u(req + pos, "Content-Type: application/x-www-form-urlencoded\r\nContent-Length: ",
                      (int)sizeof(req) - pos);
            pos = strlen_u(req);
            {
                int L = strlen_u(post), k = 0;
                char rev[8];
                int v = L;
                if (v == 0)
                    rev[k++] = '0';
                else {
                    while (v) {
                        rev[k++] = (char)('0' + v % 10);
                        v /= 10;
                    }
                }
                while (k && pos + 1 < (int)sizeof(req))
                    req[pos++] = rev[--k];
                req[pos] = 0;
            }
            pos = strlen_u(req);
            strncpy_u(req + pos, "\r\n\r\n", (int)sizeof(req) - pos);
            pos = strlen_u(req);
            strncpy_u(req + pos, post, (int)sizeof(req) - pos);
        } else {
            pos = strlen_u(req);
            strncpy_u(req + pos, "\r\n", (int)sizeof(req) - pos);
        }
        (void)tmp;
    }
    if (verbose) {
        print("> ");
        print(req);
    }
    sys_send(s, req, strlen_u(req));

    if (outfile) {
        out = sys_open(outfile, O_WRITE | O_CREATE | O_TRUNC);
        if (out < 0) {
            println("curl: cannot write -o file");
            sys_close(s);
            return 1;
        }
    }

    {
        int idle = 0;
        for (;;) {
            n = sys_recv(s, buf, sizeof(buf) - 1);
            if (n < 0)
                break;
            if (n == 0) {
                if (++idle >= 2)
                    break;
                continue;
            }
            idle = 0;
            buf[n] = 0;
            if (!hdr_done) {
                char *body = 0;
                int j;
                for (j = 0; j + 3 < n; j++) {
                    if (buf[j] == '\r' && buf[j + 1] == '\n' && buf[j + 2] == '\r' && buf[j + 3] == '\n') {
                        body = buf + j + 4;
                        buf[j] = 0;
                        hdr_done = 1;
                        break;
                    }
                }
                if (starts_with(buf, "HTTP/")) {
                    const char *sp = buf;
                    while (*sp && *sp != ' ')
                        sp++;
                    if (*sp == ' ')
                        status = atoi_u(sp + 1);
                }
                if (verbose || head_only || !outfile) {
                    print(buf);
                    if (hdr_done)
                        sys_write(1, "\n", 1);
                }
                if (head_only) {
                    sys_close(s);
                    if (out >= 0)
                        sys_close(out);
                    return status >= 400 ? 1 : 0;
                }
                if (hdr_done && body) {
                    int blen = n - (int)(body - buf);
                    if (blen > 0) {
                        if (out >= 0)
                            sys_write(out, body, blen);
                        else
                            sys_write(1, body, blen);
                    }
                } else if (!hdr_done && !outfile)
                    sys_write(1, buf, n);
                continue;
            }
            if (out >= 0)
                sys_write(out, buf, n);
            else
                sys_write(1, buf, n);
        }
    }
    sys_close(s);
    if (out >= 0)
        sys_close(out);
    if (verbose) {
        print("* status ");
        print_uint((uint32_t)status);
        sys_write(1, "\n", 1);
    }
    return status >= 400 ? 1 : 0;
}
