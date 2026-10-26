#include <kernel/vfs/initramfs.h>
#include <kernel/vfs/vfs.h>
#include <kernel/libc/string.h>
#include <kernel/mm/memory.h>
#include <kernel/log.h>
#include <kernel/errno.h>

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

#define S_IFMT  0170000
#define S_IFDIR 0040000
#define S_IFREG 0100000
#define S_IFLNK 0120000

// Directory of an archive path, creating missing directories on the way;
// the last component is returned in *leaf.
static inode_t *parent_of(char *path, char **leaf) {
    inode_t *cur = fs_root->inode;
    char *slash;

    while ((slash = strchr(path, '/')) != NULL) {
        inode_t *next;

        *slash = '\0';
        if (*path != '\0') {
            if (vfs_create(cur, path, VFS_DIRECTORY, 0755, &next) == -EEXIST) {
                next = vfs_child(cur, path)->inode;
            }
            if ((next->flags & VFS_TYPE) != VFS_DIRECTORY) {
                return NULL;
            }
            cur = next;
        }
        path = slash + 1;
    }
    *leaf = path;
    return cur;
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

// Unpack the newc cpio archive at physical [start, end) into the root
// filesystem, keeping each entry's permissions, owner and mtime.
int initramfs_parse(uint32_t start, uint32_t end) {
    klog(LOG_INFO, "initramfs", "Parsing from 0x%x to 0x%x", start, end);

    for (uint32_t at = start; at < end;) {
        cpio_newc_header_t *h = (cpio_newc_header_t *)phys_to_virt(at);
        char path[INITRAMFS_MAX_PATH];
        char *leaf;
        inode_t *dir, *n = NULL;

        if (strncmp(h->c_magic, "070701", 6) != 0) {
            klog(LOG_FAIL, "initramfs", "Invalid CPIO magic! Aborting.");
            return -1;
        }
        uint32_t namesize = hex_to_uint(h->c_namesize, 8);
        uint32_t filesize = hex_to_uint(h->c_filesize, 8);
        uint32_t mode = hex_to_uint(h->c_mode, 8);
        char *name = (char *)phys_to_virt(at + sizeof(*h));
        uint32_t data_at = (at + sizeof(*h) + namesize + 3) & ~3u;
        uint8_t *data = (uint8_t *)phys_to_virt(data_at);
        at = (data_at + filesize + 3) & ~3u;

        if (strcmp(name, "TRAILER!!!") == 0) {
            klog(LOG_INFO, "initramfs", "End of CPIO archive.");
            return 0;
        }
        if (namesize > sizeof(path)) {
            klog(LOG_FAIL, "initramfs", "Path too long: %s", name);
            continue;
        }
        strcpy(path, name);
        dir = parent_of(path, &leaf);
        if (dir == NULL || *leaf == '\0' || strcmp(leaf, ".") == 0) {
            continue;
        }

        switch (mode & S_IFMT) {
        case S_IFDIR:
            if (vfs_create(dir, leaf, VFS_DIRECTORY, mode, &n) == -EEXIST) {
                n = vfs_child(dir, leaf)->inode;
                n->mode = mode & 07777;
            }
            break;
        case S_IFREG:
        case S_IFLNK:
            if (vfs_create(dir, leaf, (mode & S_IFMT) == S_IFREG ? VFS_FILE : VFS_SYMLINK,
                           mode, &n) == 0 && filesize > 0) {
                n->write(n, 0, filesize, data);
            }
            break;
        default:
            klog(LOG_FAIL, "initramfs", "Unsupported file type (mode: 0x%x) for file: %s", mode, name);
        }
        if (n != NULL) {
            n->uid = hex_to_uint(h->c_uid, 8);
            n->gid = hex_to_uint(h->c_gid, 8);
            n->atime = n->mtime = n->ctime =
                (uint64_t)hex_to_uint(h->c_mtime, 8) * 1000000000ULL;
        }
    }
    klog(LOG_FAIL, "initramfs", "Archive ended without a trailer.");
    return -1;
}
