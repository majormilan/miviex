#include <kernel/hal/clock.h>
#include <kernel/hal/io.h>
#include <kernel/proc/scheduler.h>
#include <kernel/log.h>

volatile uint64_t jiffies;
static uint64_t boot_ns; /* wall clock at jiffies == 0 */

static uint8_t cmos(uint8_t reg) {
    outb(0x70, reg);
    return inb(0x71);
}

static uint8_t from_bcd(uint8_t v) {
    return (uint8_t)((v & 0x0F) + (v >> 4) * 10);
}

/* Days since 1970-01-01 of a proleptic Gregorian date. */
static int64_t days_from_civil(int64_t y, unsigned m, unsigned d) {
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

static void read_rtc(uint8_t r[6]) {
    while (cmos(0x0A) & 0x80) {
        ; /* update in progress */
    }
    r[0] = cmos(0x00); r[1] = cmos(0x02); r[2] = cmos(0x04);
    r[3] = cmos(0x07); r[4] = cmos(0x08); r[5] = cmos(0x09);
}

/*
 * The RTC is taken to hold UTC, as on Linux by default. The century
 * register is not standard, so years 70-99 are 19xx and the rest 20xx.
 */
void clock_init(void) {
    uint8_t a[6], b[6], fmt;
    int same;

    do {
        read_rtc(a);
        read_rtc(b);
        same = 1;
        for (int i = 0; i < 6; i++) {
            same &= a[i] == b[i];
        }
    } while (!same);

    fmt = cmos(0x0B);
    bool pm = !(fmt & 0x02) && (a[2] & 0x80);
    a[2] &= 0x7F;
    if (!(fmt & 0x04)) {
        for (int i = 0; i < 6; i++) {
            a[i] = from_bcd(a[i]);
        }
    }
    if (!(fmt & 0x02)) { /* 12-hour clock */
        a[2] = (uint8_t)(a[2] % 12 + (pm ? 12 : 0));
    }

    int64_t year = a[5] + (a[5] < 70 ? 2000 : 1900);
    int64_t days = days_from_civil(year, a[4], a[3]);
    boot_ns = (uint64_t)(days * 86400 + a[2] * 3600 + a[1] * 60 + a[0]) * NS_PER_SEC;
    klog(LOG_OK, "clock", "RTC %d-%d-%d %d:%d:%d UTC", (int)year, a[4], a[3], a[2], a[1], a[0]);
}

void clock_tick(void) {
    jiffies++;
    wakeup((void *)&jiffies); /* sleepers re-check their deadline */
}

uint64_t clock_monotonic_ns(void) {
    return jiffies * NS_PER_TICK;
}

uint64_t clock_realtime_ns(void) {
    return boot_ns + clock_monotonic_ns();
}

void clock_set_realtime_ns(uint64_t ns) {
    boot_ns = ns - clock_monotonic_ns();
}
