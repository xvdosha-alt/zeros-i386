#include "libmp.h"
#include "../../py/runtime.h"
#include "../../py/repl.h"
#include "../../py/object.h"
#include "../../py/fs.h"
#include "../../py/seed.h"
#include "../../py/host_io.h"

int main(void)
{
    char arg[128];
    int fd, n;
    arg[0] = 0;
    if (sys_exists("/sys/run/argv")) {
        fd = sys_open("/sys/run/argv", 1);
        if (fd >= 0) {
            n = sys_read(fd, arg, sizeof(arg) - 1);
            sys_close(fd);
            if (n > 0) {
                arg[n] = 0;
                while (n > 0 && (arg[n - 1] == ' ' || arg[n - 1] == '\n'))
                    arg[--n] = 0;
            } else {
                arg[0] = 0;
            }
        }
    }
    mp_io_init();
    if (arg[0]) {
        MpRuntime rt;
        MpObject *res = NULL;
        static char src[2048];
        int pfd = sys_open(arg, 1);
        if (pfd < 0) {
            print("python: open fail: ");
            println(arg);
            return 1;
        }
        n = sys_read(pfd, src, sizeof(src) - 1);
        sys_close(pfd);
        if (n < 0)
            return 1;
        src[n] = 0;
        mp_runtime_init(&rt);
        if (!mp_runtime_exec(&rt, src, &res))
            println(rt.err);
        return 0;
    }
    mp_repl_run();
    return 0;
}
