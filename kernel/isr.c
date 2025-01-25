#include "vga.h"

void isr0_handler() {
  terminal_print_colorful("Interrupt 0 triggered!\n", VGA_COLOR_LIGHT_RED);
}
