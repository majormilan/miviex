#ifndef INITRAMFS_H
#define INITRAMFS_H

#include <kernel/types.h>

void initramfs_parse(uint32_t start, uint32_t end);

#endif // INITRAMFS_H