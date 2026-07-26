#include "libmp.h"

int main(void)
{
    char arg[128];
    uint32_t ip;
    int i;

    arg[0] = 0;
    read_argv(arg, sizeof(arg));
    if (!arg[0]) {
        println("usage: ping HOST|IP");
        return 1;
    }
    {
        int j = 0;
        while (arg[j] && arg[j] != ' ')
            j++;
        arg[j] = 0;
    }
    ip = resolve_host(arg);
    if (!ip) {
        println("ping: resolve fail");
        return 1;
    }
    print("PING ");
    print(arg);
    print(" (");
    print_ip(ip);
    println(")");
    for (i = 0; i < 4; i++) {
        if (sys_ping(ip) < 0) {
            print("icmp_seq=");
            print_uint((uint32_t)i);
            println(" send fail");
        } else {
            print("icmp_seq=");
            print_uint((uint32_t)i);
            println(" sent");
        }
    }
    println("done");
    return 0;
}
