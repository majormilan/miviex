#include <kernel/vfs/vfs.h>
#include <kernel/vfs/ramfs.h>
#include <kernel/video/vga.h>
#include <kernel/mm/memory.h>

dentry_t *fs_root = (dentry_t *)0x1; // Initialize to a non-zero value to move it to .data
file_t *open_files[MAX_OPEN_FILES]; // Global file descriptor table

void vfs_init() {
    terminal_print("VFS: Address of fs_root = ");
    terminal_print_num((uintptr_t)&fs_root);
    terminal_print("\n");
    memset(&fs_root, 0, sizeof(dentry_t*));
    fs_root = ramfs_init();

    // Initialize the open_files table
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        open_files[i] = NULL;
    }
}

int open(char *path, uint32_t flags) {
    // For now, we only support opening the root directory
    if (strcmp(path, "/") == 0) {
        // Find a free file descriptor
        for (int i = 0; i < MAX_OPEN_FILES; i++) {
            if (open_files[i] == NULL) {
                file_t *file = (file_t*)k_malloc(sizeof(file_t));
                if (file == NULL) {
                    return -1; // Out of memory
                }
                memset(file, 0, sizeof(file_t));
                file->inode = fs_root->inode;
                file->offset = 0;
                file->flags = flags;
                file->ref_count = 1;
                open_files[i] = file;
                return i; // Return the file descriptor
            }
        }
    }
    return -1; // File not found or no free file descriptors
}

int close(int fd) {
    if (fd < 0 || fd >= MAX_OPEN_FILES || open_files[fd] == NULL) {
        return -1; // Invalid file descriptor
    }
    open_files[fd]->ref_count--;
    if (open_files[fd]->ref_count == 0) {
        k_free(open_files[fd]);
        open_files[fd] = NULL;
    }
    return 0;
}

uint32_t read(int fd, uint8_t *buf, uint32_t count) {
    if (fd < 0 || fd >= MAX_OPEN_FILES || open_files[fd] == NULL) {
        return 0; // Invalid file descriptor
    }
    file_t *file = open_files[fd];
    if (file->inode->read) {
        uint32_t bytes_read = file->inode->read(file->inode, file->offset, count, buf);
        file->offset += bytes_read;
        return bytes_read;
    }
    return 0;
}

uint32_t write(int fd, uint8_t *buf, uint32_t count) {
    if (fd < 0 || fd >= MAX_OPEN_FILES || open_files[fd] == NULL) {
        return 0; // Invalid file descriptor
    }
    file_t *file = open_files[fd];
    if (file->inode->write) {
        uint32_t bytes_written = file->inode->write(file->inode, file->offset, count, buf);
        file->offset += bytes_written;
        return bytes_written;
    }
    return 0;
}
