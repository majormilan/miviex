#include <kernel/vfs/initramfs.h>
#include <kernel/vfs/vfs.h>
#include <kernel/libc/string.h>
#include <kernel/mm/memory.h>
#include <kernel/log.h>
#include <kernel/vfs/ramfs.h> // For ramfs_create, ramfs_mkdir

// CPIO new ASCII format header
typedef struct cpio_newc_header {
    char    c_magic[6];
    char    c_ino[8];
    char    c_mode[8];
    char    c_uid[8];
    char    c_gid[8];
    char    c_nlink[8];
    char    c_mtime[8];
    char    c_filesize[8];
    char    c_devmajor[8];
    char    c_devminor[8];
    char    c_rdevmajor[8];
    char    c_rdevminor[8];
    char    c_namesize[8];
    char    c_check[8];
} cpio_newc_header_t;

// Maximum length (including NUL) of a single initramfs path we support.
#define INITRAMFS_MAX_PATH 256

// Walks `path` (an initramfs-relative path such as "dir/subdir/file.txt"),
// creating any missing intermediate directories under the VFS root as it
// goes. `path` is modified in place: '/' separators are replaced with '\0'
// so each component becomes its own C string. On success, returns the inode
// of the resolved parent directory and sets `*leaf_name_out` to the final
// path component (the file/dir name to create there).
static inode_t *initramfs_resolve_parent(char *path, char **leaf_name_out) {
    inode_t *current = vfs_get_root()->inode;
    char *component = path;
    char *slash;

    while ((slash = strchr(component, '/')) != NULL) {
        *slash = '\0';

        if (strlen(component) > 0) {
            inode_t *next = ramfs_finddir(current, component);
            if (next == NULL) {
                if (ramfs_mkdir(current, component, 0755) != 0) {
                    klog(LOG_FAIL, "initramfs", "Failed to create directory '%s'", component);
                    return NULL;
                }
                next = ramfs_finddir(current, component);
            } else if (!(next->flags & VFS_DIRECTORY)) {
                klog(LOG_FAIL, "initramfs", "Path component '%s' is not a directory", component);
                return NULL;
            }
            current = next;
        }

        component = slash + 1;
    }

    *leaf_name_out = component;
    return current;
}

// Helper to convert hex string to uint32_t
static uint32_t hex_to_uint(const char *hex, int len) {
    uint32_t num = 0;
    for (int i = 0; i < len; i++) {
        char c = hex[i];
        if (c >= '0' && c <= '9') {
            num = (num << 4) | (c - '0');
        } else if (c >= 'a' && c <= 'f') {
            num = (num << 4) | (c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            num = (num << 4) | (c - 'A' + 10);
        }
    }
    return num;
}

// Function to parse the initramfs (CPIO archive)
void initramfs_parse(uint32_t start, uint32_t end) {
    klog(LOG_INFO, "initramfs", "Parsing from 0x%x to 0x%x", start, end);

    uint32_t current_ptr = start;

    while (current_ptr < end) {
        cpio_newc_header_t *header = (cpio_newc_header_t *)(uintptr_t)current_ptr;

        // Check magic number
        if (strncmp(header->c_magic, "070701", 6) != 0) {
            klog(LOG_FAIL, "initramfs", "Invalid CPIO magic! Aborting.");
            break;
        }

        uint32_t namesize = hex_to_uint(header->c_namesize, 8);
        uint32_t filesize = hex_to_uint(header->c_filesize, 8);
        uint32_t mode = hex_to_uint(header->c_mode, 8);

        char *filename = (char *)(uintptr_t)(current_ptr + sizeof(cpio_newc_header_t));

        // Align header + name to 4-byte boundary
        uint32_t header_and_name_size = sizeof(cpio_newc_header_t) + namesize;
        if (header_and_name_size % 4 != 0) {
            header_and_name_size += (4 - (header_and_name_size % 4));
        }

        uint8_t *file_data = (uint8_t *)(uintptr_t)(current_ptr + header_and_name_size);

        // Align file data to 4-byte boundary
        uint32_t file_data_size_aligned = filesize;
        if (file_data_size_aligned % 4 != 0) {
            file_data_size_aligned += (4 - (file_data_size_aligned % 4));
        }

        // Check for end of archive
        if (strcmp(filename, "TRAILER!!!") == 0) {
            klog(LOG_INFO, "initramfs", "End of CPIO archive.");
            break;
        }

        klog(LOG_DEBUG, "initramfs", "Found '%s' (size: %u bytes, mode: 0x%x)", filename, filesize, mode);

        // Copy the path into a local, mutable, bounded buffer: we need to
        // split it on '/' in place, and must not risk overrunning it or
        // mutating the archive data itself.
        char path_buf[INITRAMFS_MAX_PATH];
        uint32_t path_len = strlen(filename);
        if (path_len >= INITRAMFS_MAX_PATH) {
            path_len = INITRAMFS_MAX_PATH - 1;
        }
        memcpy(path_buf, filename, path_len);
        path_buf[path_len] = '\0';

        // Determine file type and create it (walking/creating any
        // intermediate directories) in RAMFS.
        char *leaf_name = NULL;
        inode_t *parent_node = initramfs_resolve_parent(path_buf, &leaf_name);

        if (parent_node == NULL) {
            klog(LOG_FAIL, "initramfs", "Failed to resolve parent directory for: %s", filename);
        } else if (strlen(leaf_name) == 0) {
            // Path ended in '/', e.g. the directory was already created
            // while resolving intermediate components; nothing more to do.
        } else if ((mode & 0xF000) == 0x4000) { // S_IFDIR (directory)
            ramfs_mkdir(parent_node, leaf_name, mode);
        } else if ((mode & 0xF000) == 0x8000) { // S_IFREG (regular file)
            inode_t *new_file_node = ramfs_create(parent_node, leaf_name, VFS_FILE);
            if (new_file_node) {
                ramfs_write(new_file_node, 0, filesize, file_data);
            } else {
                klog(LOG_FAIL, "initramfs", "Failed to create file in ramfs: %s", filename);
            }
        } else {
            klog(LOG_FAIL, "initramfs", "Unsupported file type (mode: 0x%x) for file: %s", mode, filename);
        }

        current_ptr += header_and_name_size + file_data_size_aligned;
    }
}
