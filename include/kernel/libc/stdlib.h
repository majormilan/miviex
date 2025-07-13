#include <kernel/types.h>
#include <stdint.h>

#ifndef STDLIB_H
#define STDLIB_H

char* itoa(int value, char* str, int base);
char* uitoa(uint64_t value, char* str, int base);

#endif // STDLIB_H