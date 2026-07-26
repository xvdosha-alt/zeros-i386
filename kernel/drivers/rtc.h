#ifndef KERNEL_RTC_H
#define KERNEL_RTC_H

#include "types.h"

typedef struct {
    int year;
    int month;
    int day;
    int hour;
    int min;
    int sec;
} RtcTime;

void rtc_init(void);
/* Fill *out; always succeeds after rtc_init (PIT wall clock). */
int rtc_read(RtcTime *out);

#endif
