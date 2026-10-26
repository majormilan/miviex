#ifndef CLOCK_H
#define CLOCK_H

#include <stdint.h>

/* The PIT runs at 1193182 / 11931 Hz: one tick is 9999849 ns. */
#define CLOCK_HZ     100
#define NS_PER_TICK  9999849ULL
#define NS_PER_SEC   1000000000ULL

extern volatile uint64_t jiffies; /* timer ticks since boot */

void clock_init(void);  /* read the CMOS RTC */
void clock_tick(void);  /* from the timer interrupt */

uint64_t clock_monotonic_ns(void);
uint64_t clock_realtime_ns(void);
void clock_set_realtime_ns(uint64_t ns);

#endif /* CLOCK_H */
