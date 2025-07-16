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
void terminal_backspace(void);
void puts(const char *str);

#endif /* VGA_H */