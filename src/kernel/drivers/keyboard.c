#include <kernel/drivers/keyboard.h>
#include <kernel/hal/io.h>
#include <kernel/video/vga.h>
#include <kernel/types.h>

static char keyboard_buffer[KEYBOARD_BUFFER_SIZE];
static size_t buffer_head = 0;
static size_t buffer_tail = 0;
static bool shift_pressed = false;

keymap_entry_t keymap[] = {
    {0x01, '\x1B', '\x1B'}, {0x02, '1', '!'}, {0x03, '2', '@'}, {0x04, '3', '#'},
    {0x05, '4', '$'}, {0x06, '5', '%'}, {0x07, '6', '^'}, {0x08, '7', '&'},
    {0x09, '8', '*'}, {0x0A, '9', '('}, {0x0B, '0', ')'}, {0x0C, '-', '_'},
    {0x0D, '=', '+'}, {0x0E, '\b', '\b'}, {0x0F, '\t', '\t'}, {0x10, 'q', 'Q'},
    {0x11, 'w', 'W'}, {0x12, 'e', 'E'}, {0x13, 'r', 'R'}, {0x14, 't', 'T'},
    {0x15, 'y', 'Y'}, {0x16, 'u', 'U'}, {0x17, 'i', 'I'}, {0x18, 'o', 'O'},
    {0x19, 'p', 'P'}, {0x1A, '[', '{'}, {0x1B, ']', '}'}, {0x1C, '\n', '\n'}, // Enter
    {0x1E, 'a', 'A'}, {0x1F, 's', 'S'}, {0x20, 'd', 'D'}, {0x21, 'f', 'F'},
    {0x22, 'g', 'G'}, {0x23, 'h', 'H'}, {0x24, 'j', 'J'}, {0x25, 'k', 'K'},
    {0x26, 'l', 'L'}, {0x27, ';', ':'}, {0x28, '\'', '"'}, {0x29, '`', '~'},
    {0x2B, '\\', '|'}, {0x2C, 'z', 'Z'}, {0x2D, 'x', 'X'}, {0x2E, 'c', 'C'},
    {0x2F, 'v', 'V'}, {0x30, 'b', 'B'}, {0x31, 'n', 'N'}, {0x32, 'm', 'M'},
    {0x33, ',', '<'}, {0x34, '.', '>'}, {0x35, '/', '?'}, {0x39, ' ', ' '},
    {0x3B, '\x00', '\x00'}, {0x3C, '\x00', '\x00'}, {0x3D, '\x00', '\x00'}, // F1, F2, F3
    {0x3E, '\x00', '\x00'}, {0x3F, '\x00', '\x00'}, {0x40, '\x00', '\x00'}, // F4, F5, F6
    {0x41, '\x00', '\x00'}, {0x42, '\x00', '\x00'}, {0x43, '\x00', '\x00'}, // F7, F8, F9
    {0x44, '\x00', '\x00'}, {0x45, '\x00', '\x00'}, {0x46, '\x00', '\x00'}, // F10, Num Lock, Scroll Lock
    {0x47, '7', '7'}, {0x48, '8', '8'}, {0x49, '9', '9'}, // Keypad 7, 8, 9
    {0x4A, '-', '-'}, {0x4B, '4', '4'}, {0x4C, '5', '5'}, {0x4D, '6', '6'}, // Keypad 4, 5, 6
    {0x4E, '+', '+'}, {0x4F, '1', '1'}, {0x50, '2', '2'}, {0x51, '3', '3'}, // Keypad 1, 2, 3
    {0x52, '0', '0'}, {0x53, '.', '.'}, {0x57, '\x00', '\x00'}, {0x58, '\x00', '\x00'}, // F11, F12
    {0x1D, '\x00', '\x00'}, {0x2A, '\x00', '\x00'}, {0x36, '\x00', '\x00'}, {0x38, '\x00', '\x00'}, // Ctrl, Shift, Alt
    {0x9C, '\n', '\n'}, {0xB5, '/', '/'}, {0xC8, '8', '8'}, {0xD0, '2', '2'}, // Keypad Enter, Keypad Slash, Up Arrow, Down Arrow
    {0xCB, '4', '4'}, {0xCD, '6', '6'}, {0xC7, '7', '7'}, {0xCF, '3', '3'}, // Left Arrow, Right Arrow, Home, End
    {0xD2, '0', '0'}, {0xD3, '0', '0'}  // Insert, Delete
    // Add more keys as needed
};

int keyboard_init(void) {
    buffer_head = buffer_tail = 0;
    shift_pressed = false;
    return 0;
}

char scancode_to_ascii(uint8_t scancode) {
    if (scancode & 0x80) { // Key release
        if (scancode == 0xAA || scancode == 0xB6) { // Shift released
            shift_pressed = false;
        }
        return 0;
    }

    if (scancode == 0x2A || scancode == 0x36) { // Shift pressed
        shift_pressed = true;
        return 0;
    }

    for (size_t i = 0; i < sizeof(keymap) / sizeof(keymap[0]); i++) {
        if (keymap[i].scancode == scancode) {
            return shift_pressed ? keymap[i].shifted : keymap[i].normal;
        }
    }
    return 0; // Return 0 if scancode is not found in keymap
}

void keyboard_isr(registers_t *regs) {
    uint8_t scancode = inb(0x60);
    char c = scancode_to_ascii(scancode);
    if (c != 0) {
        size_t next = (buffer_head + 1) % KEYBOARD_BUFFER_SIZE;
        if (next != buffer_tail) { // Check if buffer is full
            keyboard_buffer[buffer_head] = c;
            buffer_head = next;
        }
    }
    outb(0x20, 0x20); // Send EOI to PIC
}

bool keyboard_get_char(char *c) {
    if (buffer_head == buffer_tail) {
        return false; // Buffer is empty
    }
    *c = keyboard_buffer[buffer_tail];
    buffer_tail = (buffer_tail + 1) % KEYBOARD_BUFFER_SIZE;
    return true;
}
