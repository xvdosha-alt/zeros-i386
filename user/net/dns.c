#include "libmp.h"

int main(void)
{
    char host[128];
    uint32_t ip;

    host[0] = 0;
    read_argv(host, sizeof(host));
    {
        int j = 0;
        while (host[j] && host[j] != ' ')
            j++;
        host[j] = 0;
    }
    if (!host[0]) {
        println("usage: dns HOST");
        return 1;
    }
    ip = resolve_host(host);
    if (!ip) {
        println("dns fail");
        return 1;
    }
    print(host);
    print(" -> ");
    print_ip(ip);
    sys_write(1, "\n", 1);
    return 0;
}
