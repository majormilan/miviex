#include <kernel/drivers/random.h>
#include <kernel/hal/clock.h>
#include <kernel/libc/string.h>
#include <kernel/log.h>

/*
 * A ChaCha20 generator. The 256-bit key is the entropy pool: events are
 * folded into it as they come, and every request rekeys from the
 * generator's own output, so earlier output cannot be recomputed.
 */

static uint32_t key[8];
static uint64_t counter;
static unsigned mixpos;

static uint32_t rotl(uint32_t v, int n) {
    return v << n | v >> (32 - n);
}

#define QR(a, b, c, d) \
    a += b; d = rotl(d ^ a, 16); c += d; b = rotl(b ^ c, 12); \
    a += b; d = rotl(d ^ a, 8);  c += d; b = rotl(b ^ c, 7)

static void chacha_block(uint32_t out[16]) {
    uint32_t x[16] = {
        0x61707865, 0x3320646e, 0x79622d32, 0x6b206574,
        key[0], key[1], key[2], key[3], key[4], key[5], key[6], key[7],
        (uint32_t)counter, (uint32_t)(counter >> 32), 0, 0,
    };
    uint32_t in[16];

    memcpy(in, x, sizeof(in));
    for (int i = 0; i < 10; i++) {
        QR(x[0], x[4], x[8], x[12]); QR(x[1], x[5], x[9], x[13]);
        QR(x[2], x[6], x[10], x[14]); QR(x[3], x[7], x[11], x[15]);
        QR(x[0], x[5], x[10], x[15]); QR(x[1], x[6], x[11], x[12]);
        QR(x[2], x[7], x[8], x[13]); QR(x[3], x[4], x[9], x[14]);
    }
    for (int i = 0; i < 16; i++) {
        out[i] = x[i] + in[i];
    }
    counter++;
}

static uint64_t rdtsc(void) {
    uint32_t lo, hi;

    asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return (uint64_t)hi << 32 | lo;
}

void random_mix(uint64_t event) {
    uint32_t *k = &key[mixpos++ % 8];

    *k = rotl(*k ^ (uint32_t)event, 7) + (uint32_t)(event >> 32) * 0x9E3779B9;
}

void random_bytes(void *buf, size_t n) {
    uint32_t block[16];
    uint8_t *p = buf;

    random_mix(rdtsc());
    while (n > 0) {
        size_t k = n < sizeof(block) ? n : sizeof(block);
        chacha_block(block);
        memcpy(p, block, k);
        p += k;
        n -= k;
    }
    chacha_block(block);       /* new key from fresh output */
    memcpy(key, block, sizeof(key));
    memset(block, 0, sizeof(block));
}

static bool hw_random(uint64_t *v, bool seed) {
    uint32_t a, b, c, d;
    uint8_t ok;

    asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(seed ? 7 : 1), "c"(0));
    if (seed ? !(b & (1u << 18)) : !(c & (1u << 30))) {
        return false;
    }
    for (int i = 0; i < 10; i++) {
        if (seed) {
            asm volatile("rdseed %0; setc %1" : "=r"(*v), "=qm"(ok));
        } else {
            asm volatile("rdrand %0; setc %1" : "=r"(*v), "=qm"(ok));
        }
        if (ok) {
            return true;
        }
    }
    return false;
}

void random_init(void) {
    uint64_t v;
    int hw = 0;

    random_mix(clock_realtime_ns());
    for (int i = 0; i < 32; i++) {
        if (hw_random(&v, true) || hw_random(&v, false)) {
            random_mix(v);
            hw++;
        }
        random_mix(rdtsc());
    }
    klog(LOG_OK, "random", "Seeded (%s)", hw ? "RDSEED/RDRAND" : "timing only, no RDRAND");
}
