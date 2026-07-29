#ifndef PIT_H
#define PIT_H

#include <stdint.h>

// Programs PIT channel 0 (wired to IRQ0) in mode 3 (square wave generator)
// to fire at approximately `frequency_hz` interrupts per second. This is
// what drives scheduler_tick() for preemptive multitasking.
void pit_init(uint32_t frequency_hz);

#endif // PIT_H
