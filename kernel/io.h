#include "types.h"
#ifndef IO_H
#define IO_H


/* Function to read a byte from an I/O port */
uint8_t inb(uint16_t port);

/* Function to write a byte to an I/O port */
void outb(uint16_t port, uint8_t data);

#endif /* IO_H */
