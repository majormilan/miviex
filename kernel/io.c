#include "io.h"

uint8_t inb(uint16_t port) {
    uint8_t result;
    __asm__ volatile ("inb %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

void outb(uint16_t port, uint8_t data) {
    __asm__ volatile ("outb %0, %1" : : "a"(data), "Nd"(port));
}

void serial_init() {
    outb(0x3F8 + 1, 0x00);    // Disable all interrupts
    outb(0x3F8 + 3, 0x80);    // Enable DLAB (set baud rate divisor)
    outb(0x3F8 + 0, 0x03);    // Set divisor to 3 (lo byte) 38400 baud
    outb(0x3F8 + 1, 0x00);    //                  (hi byte)
    outb(0x3F8 + 3, 0x03);    // Disable DLAB (set data bits to 8), no parity, 1 stop bit
    outb(0x3F8 + 2, 0xC7);    // Enable FIFO, clear them, with 14-byte threshold
    outb(0x3F8 + 4, 0x0B);    // IRQs enabled, RTS/DSR set
}

int serial_received() {
    return inb(0x3F8 + 5) & 1;
}

char serial_read() {
    while (serial_received() == 0);
    return inb(0x3F8);
}

int is_transmit_empty() {
    return inb(0x3F8 + 5) & 0x20;
}

void serial_write(char a) {
    while (is_transmit_empty() == 0);
    outb(0x3F8, a);
}
