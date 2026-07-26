#include "libmp.h"

static const char *state_name(int st)
{
    if (st == 1)
        return "ready";
    if (st == 2)
        return "run";
    if (st == 3)
        return "block";
    if (st == 4)
        return "zomb";
    return "?";
}

static void put_pad_int(int v, int width)
{
    char tmp[16];
    int i = 0, n, w;
    if (v < 0)
        v = 0;
    if (v == 0)
        tmp[i++] = '0';
    while (v > 0 && i < 15) {
        tmp[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    n = i;
    for (w = n; w < width; w++)
        sys_write(1, " ", 1);
    while (i > 0)
        sys_write(1, &tmp[--i], 1);
}

int main(void)
{
    SysProcInfo info;
    int i;

    println("  PID  PPID  STATE  NAME");
    for (i = 0; i < 64; i++) {
        if (sys_psinfo(i, &info) != 0)
            break;
        put_pad_int(info.pid, 5);
        sys_write(1, " ", 1);
        put_pad_int(info.ppid, 5);
        sys_write(1, "  ", 2);
        {
            const char *sn = state_name(info.state);
            int L = strlen_u(sn);
            sys_write(1, sn, L);
            while (L < 6) {
                sys_write(1, " ", 1);
                L++;
            }
        }
        sys_write(1, " ", 1);
        println(info.name[0] ? info.name : "?");
    }
    return 0;
}
