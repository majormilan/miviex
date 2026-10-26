#include <kernel/drivers/keyboard.h>
#include <kernel/drivers/tty.h>
#include <kernel/hal/io.h>
#include <kernel/hal/power.h>
#include <kernel/hal/apic.h>

/* Scancode set 1, 0x00-0x39. Enter is CR and Backspace DEL, as from a
 * terminal; ICRNL and VERASE take it from there. */
static const char normal[] =
    "\0" "\033" "1234567890-=" "\177" "\t" "qwertyuiop[]" "\r" "\0"
    "asdfghjkl;'`" "\0" "\\" "zxcvbnm,./" "\0" "*" "\0" " ";
static const char shifted[] =
    "\0" "\033" "!@#$%^&*()_+" "\177" "\t" "QWERTYUIOP{}" "\r" "\0"
    "ASDFGHJKL:\"~" "\0" "|" "ZXCVBNM<>?" "\0" "*" "\0" " ";
static const char keypad[] = "789-456+1230."; /* 0x47-0x53 */

static bool shift, ctrl, alt, caps, extended;

static void send(const char *s) {
    while (*s) {
        tty_input(&tty_vga, *s++);
    }
}

/* Keys behind the 0xE0 prefix: arrows and the editing block. */
static void extended_key(uint8_t code) {
    switch (code) {
    case 0x48: send("\033[A"); break;
    case 0x50: send("\033[B"); break;
    case 0x4D: send("\033[C"); break;
    case 0x4B: send("\033[D"); break;
    case 0x47: send("\033[H"); break;
    case 0x4F: send("\033[F"); break;
    case 0x49: send("\033[5~"); break;
    case 0x51: send("\033[6~"); break;
    case 0x52: send("\033[2~"); break;
    case 0x53:
        if (ctrl && alt) {
            power_ctrl_alt_del();
            break;
        }
        send("\033[3~");
        break;
    case 0x1C: tty_input(&tty_vga, '\r'); break; /* keypad Enter */
    case 0x35: tty_input(&tty_vga, '/'); break;  /* keypad / */
    }
}

int keyboard_init(void) {
    shift = ctrl = alt = caps = extended = false;
    return irq_request(1, keyboard_isr);
}

void keyboard_isr(registers_t *regs) {
    (void)regs;
    uint8_t sc = inb(0x60);
    bool release = sc & 0x80;
    uint8_t code = sc & 0x7F;
    char c;

    if (sc == 0xE0) {
        extended = true;
        return;
    }
    if (code == 0x2A || code == 0x36) {
        shift = !release;
    } else if (code == 0x1D) {
        ctrl = !release;
    } else if (code == 0x38) {
        alt = !release;
    } else if (code == 0x3A && !release) {
        caps = !caps;
    } else if (!release && extended) {
        extended_key(code);
    } else if (!release) {
        if (code < sizeof(normal) - 1) {
            c = shift ? shifted[code] : normal[code];
        } else if (code >= 0x47 && code <= 0x53) {
            c = keypad[code - 0x47];
        } else {
            c = 0; /* function keys etc. */
        }
        if (caps && c >= 'a' && c <= 'z' && !shift) {
            c = (char)(c - 32);
        } else if (caps && c >= 'A' && c <= 'Z' && shift) {
            c = (char)(c + 32);
        }
        if (ctrl && c >= '@' && c <= '~') {
            c &= 0x1F;
        }
        if (c != 0) {
            if (alt) {
                tty_input(&tty_vga, '\033');
            }
            tty_input(&tty_vga, c);
        }
    }
    extended = false;
}
