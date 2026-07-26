#include "rtc.h"
#include "timer.h"

/*
 * Wall clock without CMOS (ports 0x70/0x71 hang some QEMU/TCG hosts).
 * Seed: RTC_SEED_EPOCH from the build host; then advance with 100 Hz PIT.
 *
 * Avoid 64-bit divides and wide struct copies — zig/clang may emit SSE
 * (movaps) for those even with -mno-sse, which #UD-halts this kernel.
 */

#define TICK_HZ 100u

#ifndef RTC_SEED_EPOCH
#define RTC_SEED_EPOCH 1753462800u
#endif

static const uint8_t mdays[12] = {
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

static int ready;
static int y, mo, d, h, mi, s;
static uint32_t ticks0;

static int leap(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

static int dim(int year, int month)
{
    if (month == 2 && leap(year))
        return 29;
    if (month < 1 || month > 12)
        return 30;
    return mdays[month - 1];
}

/* Break Unix timestamp with only 32-bit ops (valid through year ~2100). */
static void from_unix32(uint32_t epoch)
{
    uint32_t days, rem, i;
    int year;

    days = epoch / 86400u;
    rem = epoch - days * 86400u;
    h = (int)(rem / 3600u);
    rem -= (uint32_t)h * 3600u;
    mi = (int)(rem / 60u);
    s = (int)(rem - (uint32_t)mi * 60u);

    year = 1970;
    for (;;) {
        uint32_t yl = leap(year) ? 366u : 365u;
        if (days < yl)
            break;
        days -= yl;
        year++;
        if (year > 2100) {
            year = 2100;
            days = 0;
            break;
        }
    }
    y = year;
    mo = 1;
    for (i = 1; i <= 12; i++) {
        int md = dim(year, (int)i);
        if (days < (uint32_t)md) {
            mo = (int)i;
            d = (int)days + 1;
            return;
        }
        days -= (uint32_t)md;
    }
    mo = 12;
    d = 31;
}

static void add_one_second(void)
{
    s++;
    if (s < 60)
        return;
    s = 0;
    mi++;
    if (mi < 60)
        return;
    mi = 0;
    h++;
    if (h < 24)
        return;
    h = 0;
    d++;
    if (d <= dim(y, mo))
        return;
    d = 1;
    mo++;
    if (mo <= 12)
        return;
    mo = 1;
    y++;
}

static void advance_by_pit(void)
{
    uint32_t now = timer_ticks();
    uint32_t elapsed = (now - ticks0) / TICK_HZ;
    if (!elapsed)
        return;
    if (elapsed > 3600u)
        elapsed = 3600u;
    ticks0 += elapsed * TICK_HZ;
    while (elapsed--)
        add_one_second();
}

void rtc_init(void)
{
    from_unix32((uint32_t)RTC_SEED_EPOCH);
    ticks0 = timer_ticks();
    ready = 1;
}

int rtc_read(RtcTime *out)
{
    if (!out)
        return -1;
    if (!ready)
        rtc_init();
    advance_by_pit();
    out->year = y;
    out->month = mo;
    out->day = d;
    out->hour = h;
    out->min = mi;
    out->sec = s;
    return 0;
}
