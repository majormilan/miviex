#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <kernel/types.h>
#include <kernel/hal/isr.h>

/* PS/2 keyboard (scancode set 1), feeding /dev/tty1 like a VT100. */
int keyboard_init(void);
void keyboard_isr(registers_t *regs);

#endif /* KEYBOARD_H */
