#ifndef RANDOM_H
#define RANDOM_H

#include <kernel/types.h>
#include <stdint.h>

void random_init(void);              /* seed: RDSEED/RDRAND, TSC, clock */
void random_mix(uint64_t event);     /* fold in an event (interrupt timing) */
void random_bytes(void *buf, size_t n);

#endif /* RANDOM_H */
