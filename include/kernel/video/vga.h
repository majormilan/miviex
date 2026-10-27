/* vga.h - Defines for VGA text mode */
#ifndef VGA_H
#define VGA_H

#include <kernel/types.h>
#include <stdint.h>

/*  VGA color palette */
#define VGA_COLOR_BLACK 0
#define VGA_COLOR_BLUE 1
#define VGA_COLOR_GREEN 2
#define VGA_COLOR_CYAN 3
#define VGA_COLOR_RED 4
#define VGA_COLOR_MAGENTA 5
#define VGA_COLOR_BROWN 6
#define VGA_COLOR_YELLOW 14 // Using LIGHT_BROWN for yellow
#define VGA_COLOR_LIGHT_GREY 7
#define VGA_COLOR_DARK_GREY 8
#define VGA_COLOR_LIGHT_BLUE 9
#define VGA_COLOR_LIGHT_GREEN 10
#define VGA_COLOR_LIGHT_CYAN 11
#define VGA_COLOR_LIGHT_RED 12
#define VGA_COLOR_LIGHT_MAGENTA 13
#define VGA_COLOR_LIGHT_BROWN 14
#define VGA_COLOR_WHITE 15

/*  VGA functions */
void terminal_clear(void);
void terminal_putchar(char c);
void terminal_print(const char *str);
void terminal_print_colorful(const char *str, unsigned char color);
void terminal_print_num(uint64_t num);
void terminal_print_hex(uint32_t num);

/* Terminal emulator on the console screen (VT100/ANSI, 256 and 24-bit
 * colors, UTF-8). Replies to queries (cursor position) go to the reply
 * hook as keyboard input. */
void vga_putc(char c);
void vga_set_reply(void (*reply)(const char *s));

/* A cell: a Unicode character and 24-bit colors. A wide character takes
 * two cells, the second marked VCELL_TAIL. */
typedef struct {
    uint32_t ch;
    uint32_t fg, bg;
} vcell_t;

#define VCELL_TAIL 0xFFFFFFFFu

/* Where the terminal draws: the VGA text mode at first, a framebuffer
 * once fb.c sets one up. draw shows one cell (both of a wide one),
 * inverted for the cursor; cells tells how wide a character is (NULL:
 * every one takes a cell). */
typedef struct {
    int cols, rows;
    void (*draw)(int col, int row, const vcell_t *c, bool cursor);
    int (*cells)(uint32_t ch);
} screen_t;

/* Switch screens, keeping what is shown; 0, or -1 without memory. */
int vga_set_screen(const screen_t *s);
void vga_size(int *rows, int *cols);

#endif /* VGA_H */