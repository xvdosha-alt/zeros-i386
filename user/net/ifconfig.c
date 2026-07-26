#include "libmp.h"

int main(void)
{
    char buf[160];
    if (sys_ifconfig(buf, sizeof(buf)) < 0) {
        println("ifconfig: fail");
        return 1;
    }
    print(buf);
    return 0;
}
