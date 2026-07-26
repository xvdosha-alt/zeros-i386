#include "libmp.h"

static void banner(void)
{
    println("");
    println("zerOS session");
    println("  1) shell (CLI)");
    println("  2) wm (GUI)");
    print("select [1/2]: ");
}

int main(void)
{
    for (;;) {
        int ch = 0;
        int pid, st, r;
        banner();
        sys_read(0, &ch, 1);
        if (ch == '2' || ch == 'd' || ch == 'D') {
            pid = sys_spawn("/sys/gui/wm");
            if (pid < 0) {
                println("wm: spawn failed");
                continue;
            }
            for (;;) {
                st = 0;
                r = sys_wait(pid, &st);
                if (r == -2) {
                    sys_yield();
                    continue;
                }
                if (r > 0)
                    break;
            }
            println("returned to session menu");
            continue;
        }
        {
            pid = sys_spawn("/sys/bin/msh");
            if (pid < 0) {
                println("shell: spawn failed");
                continue;
            }
            for (;;) {
                st = 0;
                r = sys_wait(pid, &st);
                if (r == -2) {
                    sys_yield();
                    continue;
                }
                if (r > 0)
                    break;
            }
        }
    }
    return 0;
}
