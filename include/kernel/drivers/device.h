#ifndef DEVICE_H
#define DEVICE_H

#include <kernel/vfs/vfs.h>
#include <kernel/mm/memory.h>

/* What a character device driver provides; unused entries stay NULL. */
typedef struct {
    read_type_t read;
    write_type_t write;
    ioctl_type_t ioctl;
    poll_type_t poll;
    open_type_t open;
} chardev_ops_t;

/* Publish a character device as /dev/<name> with its Linux numbers, mode
 * and group; ctx is the inode's ptr for the driver. */
inode_t *dev_register(const char *name, uint32_t major, uint32_t minor, uint32_t mode,
                      uint32_t gid, const chardev_ops_t *ops, void *ctx);

/* Physically contiguous, zeroed memory for device DMA (below 1 GiB). */
void *dma_alloc(size_t size, phys_addr_t *phys);
void dma_free(void *virt, size_t size);

/* The memory devices: null, zero, random, urandom (src/kernel/drivers/mem.c). */
void mem_init(void);

#endif /* DEVICE_H */
