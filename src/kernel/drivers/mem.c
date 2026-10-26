#include <kernel/drivers/device.h>
#include <kernel/drivers/random.h>
#include <kernel/libc/string.h>

/* /dev/null: always empty, swallows everything. */
static long null_read(inode_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    (void)node; (void)offset; (void)size; (void)buffer;
    return 0;
}

static long null_write(inode_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    (void)node; (void)offset; (void)buffer;
    return size;
}

/* /dev/zero: endless zero bytes. */
static long zero_read(inode_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    (void)node; (void)offset;
    memset(buffer, 0, size);
    return size;
}

/* /dev/random, /dev/urandom: the kernel generator; writes are mixed in. */
static long random_read(inode_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    (void)node; (void)offset;
    random_bytes(buffer, size);
    return size;
}

static long random_write(inode_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    (void)node; (void)offset;
    for (uint32_t i = 0; i + 8 <= size; i += 8) {
        uint64_t v;
        memcpy(&v, buffer + i, 8);
        random_mix(v);
    }
    return size;
}

static const chardev_ops_t null_ops = { .read = null_read, .write = null_write };
static const chardev_ops_t zero_ops = { .read = zero_read, .write = null_write };
static const chardev_ops_t random_ops = { .read = random_read, .write = random_write };

void mem_init(void) {
    dev_register("null", 1, 3, 0666, 0, &null_ops, NULL);
    dev_register("zero", 1, 5, 0666, 0, &zero_ops, NULL);
    dev_register("random", 1, 8, 0666, 0, &random_ops, NULL);
    dev_register("urandom", 1, 9, 0666, 0, &random_ops, NULL);
}
