#include "libmp.h"

int main(void)
{
    int pid = sys_spawn("/sys/gui/wm");
    int st = 0;
    int r;
    if (pid < 0) {
        println("desktop: spawn failed");
        return 1;
    }
    /* wm cooperatively yields (-2) on console I/O. Keep waiting until
     * wait returns a positive pid (reaped zombie). Never treat a yield
     * or "still running" as exit — that blanks the framebuffer. */
    for (;;) {
        st = 0;
        r = sys_wait(pid, &st);
        if (r > 0)
            break;
    }
    sys_fb_mode(0, 0, 0);
    return 0;
}
