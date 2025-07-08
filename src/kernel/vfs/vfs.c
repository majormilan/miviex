#include <kernel/vfs/vfs.h>
#include <kernel/vfs/ramfs.h>
#include <kernel/video/vga.h>
#include <kernel/libc/string.h>
#include <kernel/mm/memory.h>
#include <kernel/libc/stdlib.h>

dentry_t *fs_root = (dentry_t *)0x1; // Initialize to a non-zero value to move it to .data
file_t *open_files[MAX_OPEN_FILES]; // Global file descriptor table

dentry_t* vfs_init() {
    memset(&fs_root, 0, sizeof(dentry_t*));
    fs_root = ramfs_init();

    // Initialize the open_files table
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        open_files[i] = NULL;
    }
    return fs_root;
}

int open(char *path, uint32_t flags) {
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
        strcpy(parent_path, "/");
        strcpy(filename, ""); // Should not happen for file creation
    } else {
        strncpy(parent_path, path, last_slash);
        parent_path[last_slash] = '\0';
        strcpy(filename, path + last_slash + 1);
    }

    inode_t *parent_node = vfs_lookup(fs_root->inode, parent_path);
    if (parent_node == NULL) {
        terminal_print_colorful("VFS: Parent directory not found for open!\n", VGA_COLOR_RED);
        return -1;
    }

    inode_t *node = vfs_lookup(parent_node, filename);

    if (node == NULL) {
        // File does not exist, try to create it if O_CREAT is set
        if (flags & O_CREAT) {
            node = vfs_create(parent_node, filename, VFS_FILE);
            if (node == NULL) {
                terminal_print_colorful("VFS: Failed to create file!\n", VGA_COLOR_RED);
                return -1;
            }
        } else {
            return -1; // File not found and O_CREAT not set
        }
    }

    // Find a free file descriptor
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (open_files[i] == NULL) {
            file_t *file = (file_t*)k_malloc(sizeof(file_t));
            if (file == NULL) {
                return -1; // Out of memory
            }
            memset(file, 0, sizeof(file_t));
            file->inode = node;
            file->offset = 0;
            file->flags = flags;
            file->ref_count = 1;
            open_files[i] = file;
            return i; // Return the file descriptor
        }
    }
    return -1; // No free file descriptors
}

inode_t* vfs_create(inode_t *parent, char *name, uint32_t flags) {
    if (parent->create) {
        return parent->create(parent, name, flags);
    }
    return NULL;
}

inode_t* vfs_lookup(inode_t *parent, char *path) {
    terminal_print_colorful("vfs_lookup: Looking up path: ", VGA_COLOR_YELLOW);
    terminal_print_colorful(path, VGA_COLOR_YELLOW);
    terminal_print_colorful(" from parent: ", VGA_COLOR_YELLOW);
    terminal_print_colorful(parent->name, VGA_COLOR_YELLOW);
    terminal_print_colorful("\n", VGA_COLOR_YELLOW);

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
        terminal_print_colorful("vfs_lookup: Failed to allocate path_copy!\n", VGA_COLOR_RED);
        return NULL; // Out of memory
    }
    strcpy(path_copy, path);

    char *token = path_copy;
    char *next_token;
    inode_t *current_node = parent;

    while ((next_token = strchr(token, '/')) != NULL) {
        *next_token = '\0'; // Null-terminate the current component
        if (strlen(token) > 0) { // Skip empty tokens (e.g., "//")
            terminal_print_colorful("vfs_lookup: Finding token: ", VGA_COLOR_YELLOW);
            terminal_print_colorful(token, VGA_COLOR_YELLOW);
            terminal_print_colorful(" in ", VGA_COLOR_YELLOW);
            terminal_print_colorful(current_node->name, VGA_COLOR_YELLOW);
            terminal_print_colorful("\n", VGA_COLOR_YELLOW);

            if (current_node->finddir) {
                current_node = current_node->finddir(current_node, token);
                if (current_node == NULL) {
                    terminal_print_colorful("vfs_lookup: Token not found!\n", VGA_COLOR_RED);
                    k_free(path_copy);
                    return NULL; // Component not found
                }
            }
            else {
                terminal_print_colorful("vfs_lookup: Not a directory (no finddir)!\n", VGA_COLOR_RED);
                k_free(path_copy);
                return NULL; // Not a directory
            }
        }
        token = next_token + 1; // Move to the next component
    }

    // Process the last component
    if (strlen(token) > 0) {
        terminal_print_colorful("vfs_lookup: Processing last token: ", VGA_COLOR_YELLOW);
        terminal_print_colorful(token, VGA_COLOR_YELLOW);
        terminal_print_colorful(" in ", VGA_COLOR_YELLOW);
        terminal_print_colorful(current_node->name, VGA_COLOR_YELLOW);
        terminal_print_colorful("\n", VGA_COLOR_YELLOW);

        if (current_node->finddir) {
            current_node = current_node->finddir(current_node, token);
        }
        else {
            terminal_print_colorful("vfs_lookup: Not a directory (no finddir for last token)!\n", VGA_COLOR_RED);
            k_free(path_copy);
            return NULL; // Not a directory
        }
    }

    k_free(path_copy);
    terminal_print_colorful("vfs_lookup: Returning node for: ", VGA_COLOR_GREEN);
    if (current_node) terminal_print_colorful(current_node->name, VGA_COLOR_GREEN);
    terminal_print_colorful("\n", VGA_COLOR_GREEN);
    return current_node;
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

void vfs_mount(dentry_t *parent, dentry_t *child) {
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

int lseek(int fd, int offset, int whence) {
    if (fd < 0 || fd >= MAX_OPEN_FILES || open_files[fd] == NULL) {
        return -1; // Invalid file descriptor
    }

    file_t *file = open_files[fd];
    int new_offset;

    switch (whence) {
        case SEEK_SET:
            new_offset = offset;
            break;
        case SEEK_CUR:
            new_offset = file->offset + offset;
            break;
        case SEEK_END:
            new_offset = file->inode->length + offset;
            break;
        default:
            return -1; // Invalid whence
    }

    if (new_offset < 0) {
        return -1; // Cannot seek before beginning of file
    }

    file->offset = new_offset;
    return new_offset;
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
