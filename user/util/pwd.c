#include "libmp.h"

int main(void)
{
    char cwd[128];
    sys_getcwd(cwd, sizeof(cwd));
    println(cwd);
    return 0;
}
