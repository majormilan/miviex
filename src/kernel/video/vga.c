/*  vga.c - VGA and Terminal Driver Implementation */
#include <kernel/video/vga.h>
#include <kernel/libc/stdlib.h>
#include <kernel/mm/memory.h>
#include <kernel/types.h>
#include <kernel/hal/io.h>

#define VGA_ADDRESS 0xB8000
#define VGA_WIDTH 80
#define VGA_HEIGHT 25

/*  VGA video memory */
volatile unsigned short *const VGA = (unsigned short *)VGA_ADDRESS;

unsigned int terminal_row = 0;
unsigned int terminal_column = 0;
unsigned char terminal_color = VGA_COLOR_LIGHT_GREY | VGA_COLOR_BLACK << 4;

void terminal_scroll();

void terminal_putchar(char c) {
  serial_write(c);
  if (c == '\n') {
    terminal_row++;
    terminal_column = 0;
    if (terminal_row >= VGA_HEIGHT) {
      terminal_scroll();
    }
  } else {
    unsigned int index = (terminal_row * VGA_WIDTH + terminal_column);
    VGA[index] = (c | (terminal_color << 8));
    terminal_column++;
    if (terminal_column >= VGA_WIDTH) {
      terminal_column = 0;
      terminal_row++;
      if (terminal_row >= VGA_HEIGHT) {
        terminal_scroll();
      }
    }
  }
}

void terminal_scroll() {
  for (int i = 0; i < VGA_HEIGHT - 1; i++) {
    for (int j = 0; j < VGA_WIDTH; j++) {
      VGA[i * VGA_WIDTH + j] = VGA[(i + 1) * VGA_WIDTH + j];
    }
  }
  for (int j = 0; j < VGA_WIDTH; j++) {
    VGA[(VGA_HEIGHT - 1) * VGA_WIDTH + j] =
        (0 | (terminal_color << 8)); /*  Clear last row */
  }
  terminal_row = VGA_HEIGHT - 1;
}

void terminal_clear() {
  for (unsigned int i = 0; i < VGA_HEIGHT * VGA_WIDTH; i++) {
    VGA[i] = (0 | (terminal_color << 8)); /*  Clear screen */
  }
  terminal_row = 0;
  terminal_column = 0;
}

void terminal_print(const char *str) {
  while (*str) {
    terminal_putchar(*str++);
  }
}

void terminal_print_colorful(const char *str, unsigned char color) {
  unsigned char original_color = terminal_color;
  terminal_color = color;
  terminal_print(str);
  terminal_color = original_color;
}

void terminal_print_num(uint64_t num) {
  char buf[50];
  uitoa(num, buf, 10);
  terminal_print(buf);
}

void terminal_backspace() {    if (terminal_column > 0) {        terminal_column--;        unsigned int index = (terminal_row * VGA_WIDTH + terminal_column);        VGA[index] = (' ' | (terminal_color << 8));    }}void puts(const char *str) {    terminal_print(str);    terminal_putchar('\n');}