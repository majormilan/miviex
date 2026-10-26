#include <kernel/vfs/vfs.h>
#include <kernel/vfs/ramfs.h>
#include <kernel/video/vga.h>
#include <kernel/libc/string.h>
#include <kernel/mm/memory.h>
#include <kernel/libc/stdlib.h>

dentry_t *fs_root = (dentry_t *)0x1; // Initialize to a non-zero value to move it to .data

dentry_t* vfs_init() {
    memset(&fs_root, 0, sizeof(dentry_t*));
    fs_root = ramfs_init();

    // Initialize devfs and mount it under /dev
    dentry_t *dev_dentry = devfs_init();
    if (dev_dentry != NULL) {
        // Create /dev directory in ramfs
        inode_t *dev_dir_inode = ramfs_create(fs_root->inode, "dev", VFS_DIRECTORY);
        if (dev_dir_inode != NULL) {
            vfs_mount((dentry_t*)dev_dir_inode->ptr, dev_dentry);
            terminal_print_colorful("VFS: Mounted /dev filesystem.\n", VGA_COLOR_GREEN);
        } else {
            terminal_print_colorful("VFS: Failed to create /dev directory in ramfs.\n", VGA_COLOR_RED);
        }
    } else {
        terminal_print_colorful("VFS: Failed to initialize devfs.\n", VGA_COLOR_RED);
    }

    terminal_print_colorful("VFS: Initialized. Current VFS tree:\n", VGA_COLOR_CYAN);
    vfs_debug_print_tree(fs_root, 0);

    return fs_root;
}

inode_t* vfs_create(inode_t *parent, char *name, uint32_t flags) {
    if (parent->create) {
        return parent->create(parent, name, flags);
    }
    return NULL;
}

// One path component. Directory inodes keep their dentry in ->ptr (ramfs,
// devfs), which is how ".." finds the parent; "/.." is "/".
static inode_t *step(inode_t *dir, char *name) {
    if (strcmp(name, ".") == 0) {
        return dir;
    }
    if (strcmp(name, "..") == 0) {
        dentry_t *d = (dentry_t *)dir->ptr;
        return d != NULL && d->parent != NULL ? d->parent->inode : dir;
    }
    return dir->finddir(dir, name);
}

inode_t* vfs_lookup(inode_t *parent, char *path) {
    if (strcmp(path, "/") == 0) {
        return fs_root->inode;
    }

    // Handle absolute paths
    if (path[0] == '/') {
        parent = fs_root->inode;
        path++; // Skip the leading slash
    }

    char *path_copy = (char*)k_malloc(strlen(path) + 1);
    if (path_copy == NULL) {
        return NULL; // Out of memory
    }
    strcpy(path_copy, path);

    char *token = path_copy;
    char *next_token;
    inode_t *current_node = parent;

    while ((next_token = strchr(token, '/')) != NULL) {
        *next_token = '\0'; // Null-terminate the current component
        if (strlen(token) > 0) { // Skip empty tokens (e.g., "//")
            if (current_node->finddir) {
                current_node = step(current_node, token);
                if (current_node == NULL) {
                    k_free(path_copy);
                    return NULL; // Component not found
                }
            }
            else {
                k_free(path_copy);
                return NULL; // Not a directory
            }
        }
        token = next_token + 1; // Move to the next component
    }

    // Process the last component
    if (strlen(token) > 0) {
        if (current_node->finddir) {
            current_node = step(current_node, token);
        }
        else {
            k_free(path_copy);
            return NULL; // Not a directory
        }
    }

    k_free(path_copy);
    return current_node;
}

void vfs_add_child(dentry_t *parent, dentry_t *child) {
    child->parent = parent;
    if (parent->first_child == NULL) {
        parent->first_child = child;
    } else {
        dentry_t *current = parent->first_child;
        while (current->next_sibling != NULL) {
            current = current->next_sibling;
        }
        current->next_sibling = child;
    }
}

// Graft a filesystem root onto an existing directory: the mountpoint takes
// over the root's inode and children, so lookups resolve straight through.
void vfs_mount(dentry_t *mountpoint, dentry_t *root) {
    mountpoint->inode = root->inode;
    mountpoint->first_child = root->first_child;
    for (dentry_t *c = mountpoint->first_child; c != NULL; c = c->next_sibling) {
        c->parent = mountpoint;
    }
    root->inode->ptr = mountpoint;
    k_free(root);
}

void vfs_debug_print_tree(dentry_t *dentry, int level) {
    for (int i = 0; i < level; i++) {
        terminal_print("|   ");
    }
    terminal_print("|-- ");
    terminal_print(dentry->name);
    terminal_print("\n");

    dentry_t *child = dentry->first_child;
    while (child != NULL) {
        vfs_debug_print_tree(child, level + 1);
        child = child->next_sibling;
    }
}

int mkdir(char *path, uint32_t mode) {
    char parent_path[128];
    char dirname[128];
    int last_slash = -1;

    for (int i = 0; path[i] != '\0'; i++) {
        if (path[i] == '/') {
            last_slash = i;
        }
    }

    if (last_slash == -1) { // No slash, directory in root
        strcpy(parent_path, "/");
        strcpy(dirname, path);
    } else if (last_slash == 0 && path[1] == '\0') { // Path is just "/"
        return -1; // Cannot create root directory
    } else {
        strncpy(parent_path, path, last_slash);
        parent_path[last_slash] = '\0';
        strcpy(dirname, path + last_slash + 1);
    }

    inode_t *parent_node = vfs_lookup(fs_root->inode, parent_path);
    if (parent_node == NULL) {
        terminal_print_colorful("VFS: Parent directory not found for mkdir!\n", VGA_COLOR_LIGHT_RED);
        return -1;
    }

    if (parent_node->mkdir) {
        return parent_node->mkdir(parent_node, dirname, mode);
    }
    return -1; // Filesystem does not support mkdir
}

int rmdir(char *path) {
    char parent_path[128];
    char dirname[128];
    int last_slash = -1;

    for (int i = 0; path[i] != '\0'; i++) {
        if (path[i] == '/') {
            last_slash = i;
        }
    }

    if (last_slash == -1) { // No slash, directory in root
        strcpy(parent_path, "/");
        strcpy(dirname, path);
    } else if (last_slash == 0 && path[1] == '\0') { // Path is just "/"
        return -1; // Cannot remove root directory
    } else {
        strncpy(parent_path, path, last_slash);
        parent_path[last_slash] = '\0';
        strcpy(dirname, path + last_slash + 1);
    }

    inode_t *parent_node = vfs_lookup(fs_root->inode, parent_path);
    if (parent_node == NULL) {
        terminal_print_colorful("VFS: Parent directory not found for rmdir!\n", VGA_COLOR_LIGHT_RED);
        return -1;
    }

    if (parent_node->rmdir) {
        return parent_node->rmdir(parent_node, dirname);
    }
    return -1; // Filesystem does not support rmdir
}

int unlink(char *path) {
    char parent_path[128];
    char filename[128];
    int last_slash = -1;

    for (int i = 0; path[i] != '\0'; i++) {
        if (path[i] == '/') {
            last_slash = i;
        }
    }

    if (last_slash == -1) { // No slash, file in root
        strcpy(parent_path, "/");
        strcpy(filename, path);
    } else if (last_slash == 0 && path[1] == '\0') { // Path is just "/"
        return -1; // Cannot unlink root directory
    } else {
        strncpy(parent_path, path, last_slash);
        parent_path[last_slash] = '\0';
        strcpy(filename, path + last_slash + 1);
    }

    inode_t *parent_node = vfs_lookup(fs_root->inode, parent_path);
    if (parent_node == NULL) {
        terminal_print_colorful("VFS: Parent directory not found for unlink!\n", VGA_COLOR_LIGHT_RED);
        return -1;
    }

    if (parent_node->unlink) {
        return parent_node->unlink(parent_node, filename);
    }
    return -1; // Filesystem does not support unlink
}

int stat(char *path, stat_t *buf) {
    inode_t *node = vfs_lookup(fs_root->inode, path);
    if (node == NULL) {
        terminal_print_colorful("VFS: File not found for stat!\n", VGA_COLOR_LIGHT_RED);
        return -1;
    }

    if (node->stat) {
        return node->stat(node, buf);
    }
    return -1; // Filesystem does not support stat
}

// Global VFS root
extern dentry_t *fs_root;

// Function to get the global VFS root
dentry_t* vfs_get_root() {
    return fs_root;
}
