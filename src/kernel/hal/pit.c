#include <kernel/hal/pit.h>
#include <kernel/hal/io.h>

#define PIT_CHANNEL0_DATA   0x40
#define PIT_COMMAND         0x43
#define PIT_BASE_FREQUENCY  1193182u // Hz, the PIT's fixed input clock

void pit_init(uint32_t frequency_hz) {
    if (frequency_hz == 0) {
        frequency_hz = 100;
    }

    uint32_t divisor = PIT_BASE_FREQUENCY / frequency_hz;
    if (divisor == 0) {
        divisor = 1;
    } else if (divisor > 0xFFFF) {
        divisor = 0xFFFF; // 16-bit reload register, ~18.2Hz slowest rate
    }

    // Channel 0, access mode lobyte/hibyte, mode 3 (square wave generator)
    outb(PIT_COMMAND, 0x36);
    outb(PIT_CHANNEL0_DATA, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0_DATA, (uint8_t)((divisor >> 8) & 0xFF));
}
