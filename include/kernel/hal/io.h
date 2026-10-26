#include <kernel/types.h>
#include <stdint.h>
#ifndef IO_H
#define IO_H


/* Function to read a byte from an I/O port */
uint8_t inb(uint16_t port);

/* Function to write a byte to an I/O port */
void outb(uint16_t port, uint8_t data);

static inline void outw(uint16_t port, uint16_t data) {
    asm volatile("outw %0, %1" : : "a"(data), "Nd"(port));
}

static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    asm volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static inline void wrmsr(uint32_t msr, uint64_t v) {
    asm volatile("wrmsr" : : "c"(msr), "a"((uint32_t)v), "d"((uint32_t)(v >> 32)));
}

void serial_init();
int serial_received();
char serial_read();
int is_transmit_empty();
void serial_write(char a);

#endif /* IO_H */
