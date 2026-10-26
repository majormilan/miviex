#include <kernel/vfs/vfs.h>
#include <kernel/libc/string.h>
#include <kernel/mm/memory.h>
#include <kernel/log.h>
#include <kernel/hal/io.h>
#include <kernel/vfs/ramfs.h>
#include <kernel/video/vga.h>
#include <kernel/drivers/keyboard.h>
#include <kernel/mm/vmm.h>
#include <kernel/errno.h>

static uint32_t next_devfs_inode = 1;

// /dev/console: VGA + serial output; input from the PS/2 keyboard or the
// serial port (the only console on many headless and cloud hypervisors).

// Next input byte, sleeping until one arrives. Syscalls run with IF clear,
// so interrupts are enabled while we wait and other processes keep running.
static char console_getc(void) {
    uint64_t rflags;
    char c;

    asm volatile("pushfq; pop %0" : "=r"(rflags));
    for (;;) {
        if (keyboard_get_char(&c)) {
            break;
        }
        if (serial_received()) {
            c = (char)inb(0x3F8);
            break;
        }
        asm volatile("sti; hlt; cli");
    }
    if (rflags & 0x200) {
        asm volatile("sti");
    }
    if (c == '\r') {
        return '\n';
    }
    return c == 0x7F ? '\b' : c;
}

// Canonical mode: collect and echo a line (backspace edits it, ^D ends
// input), then hand it out across as many reads as it takes.
static uint32_t console_read(inode_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    (void)node; (void)offset;
    static char line[256];
    static uint32_t len, pos;
    uint32_t n;

    if (pos == len) {
        pos = len = 0;
        for (;;) {
            char c = console_getc();
            if (c == 4) {
                break;
            }
            if (c == '\b') {
                if (len > 0) {
                    len--;
                    terminal_backspace();
                    serial_write('\b'); serial_write(' '); serial_write('\b');
                }
                continue;
            }
            if (len < sizeof(line) - 1 || c == '\n') {
                line[len++] = c;
                terminal_putchar(c);
            }
            if (c == '\n') {
                break;
            }
        }
    }
    n = len - pos < size ? len - pos : size;
    memcpy(buffer, line + pos, n);
    pos += n;
    return n;
}

#define TCGETS     0x5401
#define TCSETS     0x5402
#define TCSETSW    0x5403
#define TCSETSF    0x5404
#define TIOCGWINSZ 0x5413
#define TIOCSWINSZ 0x5414

struct winsize {
    uint16_t row, col, xpixel, ypixel;
};

struct termios {
    uint32_t iflag, oflag, cflag, lflag;
    uint8_t line;
    uint8_t cc[19];
};

// Reports a fixed 80x25 cooked-mode terminal; mode changes are accepted
// and ignored until there is a real line discipline.
static long console_ioctl(inode_t *node, unsigned long req, uint64_t arg) {
    (void)node;
    switch (req) {
    case TIOCGWINSZ:
        if (!vmm_user_ok(arg, sizeof(struct winsize), true)) {
            return -EFAULT;
        }
        *(struct winsize *)arg = (struct winsize){ 25, 80, 0, 0 };
        return 0;
    case TCGETS: {
        if (!vmm_user_ok(arg, sizeof(struct termios), true)) {
            return -EFAULT;
        }
        struct termios t = {
            .iflag = 0x100,                      /* ICRNL */
            .oflag = 0x1 | 0x4,                  /* OPOST | ONLCR */
            .cflag = 0xF | 0x30 | 0x80,          /* B38400 | CS8 | CREAD */
            .lflag = 0x1 | 0x2 | 0x8 | 0x10 | 0x20 | 0x8000, /* ISIG ICANON ECHO ECHOE ECHOK IEXTEN */
            .cc = { 3, 0x1C, 0x7F, 0x15, 4, 0, 1 },
        };
        *(struct termios *)arg = t;
        return 0;
    }
    case TCSETS:
    case TCSETSW:
    case TCSETSF:
    case TIOCSWINSZ:
        return 0;
    }
    return -EINVAL;
}

static uint32_t console_write(inode_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    (void)node; (void)offset;
    for (uint32_t i = 0; i < size; i++) {
        terminal_putchar((char)buffer[i]);
    }
    return size;
}

// /dev/keyboard: a read-only character device draining the keyboard
// driver's internal ring buffer (keyboard_get_char()). Non-blocking: a read
// returns as many bytes as are currently available (possibly 0) rather
// than waiting for more input.
static uint32_t keyboard_read(inode_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    (void)node; (void)offset;
    uint32_t read_count = 0;
    char c;
    while (read_count < size && keyboard_get_char(&c)) {
        buffer[read_count++] = (uint8_t)c;
    }
    return read_count;
}

static uint32_t keyboard_write(inode_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    (void)node; (void)offset; (void)size; (void)buffer;
    // Input-only device: writes are silently discarded, matching typical
    // Unix behavior for read-only device nodes.
    return 0;
}

void dev_open(inode_t *node) {
    klog(LOG_DEBUG, "devfs", "Opening device %s", node->name);
}

void dev_close(inode_t *node) {
    klog(LOG_DEBUG, "devfs", "Closing device %s", node->name);
}

int devfs_stat(inode_t *node, stat_t *buf) {
    if (node == NULL || buf == NULL) {
        return -1; // Invalid arguments
    }

    buf->st_ino = node->inode; // Inode number
    buf->st_mode = node->flags; // File type and permissions
    buf->st_nlink = 1; // For now, assume 1 hard link
    buf->st_size = node->length; // Size of file

    return 0;
}

// Allocates and links a new character-device inode/dentry as a child of
// dev_root, wired up with the given read/write implementations. Mirrors
// ramfs_mkdir()'s inode+dentry allocation/linking pattern.
static inode_t* devfs_add_device(dentry_t *dev_root, const char *name, read_type_t read_fn, write_type_t write_fn, ioctl_type_t ioctl_fn) {
    inode_t *node = (inode_t*)k_malloc(sizeof(inode_t));
    dentry_t *dentry = (dentry_t*)k_malloc(sizeof(dentry_t));
    if (node == NULL || dentry == NULL) {
        klog(LOG_FAIL, "devfs", "Failed to allocate device node '%s'", name);
        k_free(node);
        k_free(dentry);
        return NULL;
    }

    memset(node, 0, sizeof(inode_t));
    strcpy(node->name, name);
    node->flags = VFS_CHARDEVICE;
    node->inode = next_devfs_inode++;
    node->read = read_fn;
    node->write = write_fn;
    node->ioctl = ioctl_fn;
    node->open = dev_open;
    node->close = dev_close;
    node->stat = devfs_stat;

    memset(dentry, 0, sizeof(dentry_t));
    strcpy(dentry->name, name);
    dentry->inode = node;
    vfs_add_child(dev_root, dentry);

    klog(LOG_DEBUG, "devfs", "Created device node '%s'", name);
    return node;
}

// Walks dev_root's dentry children by name -- mirrors ramfs_finddir()'s
// pattern -- so vfs_lookup() can actually resolve paths like
// "/dev/console" down through devfs's own root inode (previously this
// inode had no finddir set at all, so any lookup past "dev" itself failed).
static inode_t* devfs_finddir(inode_t *node, char *name) {
    if (!(node->flags & VFS_DIRECTORY)) {
        return NULL;
    }
    dentry_t *dir_dentry = (dentry_t*)node->ptr;
    dentry_t *child = dir_dentry->first_child;
    while (child != NULL) {
        if (strcmp(child->name, name) == 0) {
            return child->inode;
        }
        child = child->next_sibling;
    }
    return NULL;
}

dentry_t* devfs_init() {
    klog(LOG_INFO, "devfs", "Initializing...");

    // Create a root inode for /dev
    inode_t *dev_root_inode = (inode_t*)k_malloc(sizeof(inode_t));
    dentry_t *dev_root_dentry = (dentry_t*)k_malloc(sizeof(dentry_t));
    if (dev_root_inode == NULL) {
        klog(LOG_FAIL, "devfs", "Failed to allocate memory for /dev inode!");
        return NULL;
    }
    memset(dev_root_inode, 0, sizeof(inode_t));
    strcpy(dev_root_inode->name, "dev");
    dev_root_inode->flags = VFS_DIRECTORY;
    dev_root_inode->ptr = (void*)dev_root_dentry;
    dev_root_inode->stat = devfs_stat;
    dev_root_inode->finddir = devfs_finddir;

    // Create a dentry for /dev
    if (dev_root_dentry == NULL) {
        klog(LOG_FAIL, "devfs", "Failed to allocate memory for /dev dentry!");
        k_free(dev_root_inode);
        return NULL;
    }
    memset(dev_root_dentry, 0, sizeof(dentry_t));
    strcpy(dev_root_dentry->name, "dev");
    dev_root_dentry->inode = dev_root_inode;
    dev_root_dentry->parent = NULL;
    dev_root_dentry->first_child = NULL;
    dev_root_dentry->next_sibling = NULL;

    devfs_add_device(dev_root_dentry, "console", console_read, console_write, console_ioctl);
    devfs_add_device(dev_root_dentry, "keyboard", keyboard_read, keyboard_write, NULL);

    klog(LOG_OK, "devfs", "Initialized.");
    return dev_root_dentry;
}
