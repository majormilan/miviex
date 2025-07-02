#include "vfs.h"
#include "ramfs.h"
#include "vga.h"
#include "memory.h"

vfs_node_t *fs_root = (vfs_node_t *)0x1; // Initialize to a non-zero value to move it to .data

void vfs_init() {    terminal_print("VFS: Address of fs_root = ");    terminal_print_num((uintptr_t)&fs_root);    terminal_print("\n");    memset(&fs_root, 0, sizeof(vfs_node_t*));    fs_root = ramfs_init();}
