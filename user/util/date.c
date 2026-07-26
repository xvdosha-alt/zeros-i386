#include "libmp.h"

static void put2(char *dst, int v)
{
    if (v < 0)
        v = 0;
    if (v > 99)
        v = 99;
    dst[0] = (char)('0' + (v / 10));
    dst[1] = (char)('0' + (v % 10));
}

int main(void)
{
    SysTime t;
    char buf[32];
    int y;

    if (sys_time(&t) != 0) {
        println("date: no RTC");
        return 1;
    }
    y = t.year;
    buf[0] = (char)('0' + (y / 1000) % 10);
    buf[1] = (char)('0' + (y / 100) % 10);
    buf[2] = (char)('0' + (y / 10) % 10);
    buf[3] = (char)('0' + (y % 10));
    buf[4] = '-';
    put2(buf + 5, t.month);
    buf[7] = '-';
    put2(buf + 8, t.day);
    buf[10] = ' ';
    put2(buf + 11, t.hour);
    buf[13] = ':';
    put2(buf + 14, t.min);
    buf[16] = ':';
    put2(buf + 17, t.sec);
    buf[19] = 0;
    println(buf);
    return 0;
}
