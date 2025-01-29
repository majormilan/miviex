#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "types.h"  // Include your custom types header

#define KEYBOARD_BUFFER_SIZE 128

typedef struct {
    uint8_t scancode;
    char normal;
    char shifted;
} keymap_entry_t;

extern keymap_entry_t keymap[];

void keyboard_init(void);
char scancode_to_ascii(uint8_t scancode);
void keyboard_isr(void);
bool keyboard_get_char(char *c);

#endif /* KEYBOARD_H */
