/*  vga.c - VGA text mode with a small VT100/ANSI terminal emulator */
#include <kernel/video/vga.h>
#include <kernel/libc/stdlib.h>
#include <kernel/libc/string.h>
#include <kernel/mm/memory.h>
#include <kernel/types.h>
#include <kernel/hal/io.h>

#define VGA_ADDRESS 0xB8000
#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define MAX_PARAMS 8

/*  VGA video memory */
static volatile uint16_t *const VGA = (uint16_t *)(VGA_ADDRESS + KERNEL_BASE);

/* ANSI color index -> VGA color */
static const uint8_t ansi_vga[8] = { 0, 4, 2, 6, 1, 5, 3, 7 };

static int row, col, saved_row, saved_col;
static uint8_t fg = VGA_COLOR_LIGHT_GREY, bg = VGA_COLOR_BLACK;
static bool bright, reverse;
static enum { NORMAL, ESC, CSI } state;
static int params[MAX_PARAMS], nparams;
static bool private_seq;
static void (*reply)(const char *);

static uint16_t cell(char c) {
    uint8_t f = (uint8_t)(fg | (bright ? 8 : 0)), b = bg;

    if (reverse) {
        uint8_t t = f; f = b; b = t;
    }
    return (uint16_t)((uint8_t)c | (uint16_t)(f | b << 4) << 8);
}

static void clear_cells(int from, int to) {
    for (int i = from; i < to; i++) {
        VGA[i] = cell(' ');
    }
}

static void scroll(void) {
    for (int i = 0; i < (VGA_HEIGHT - 1) * VGA_WIDTH; i++) {
        VGA[i] = VGA[i + VGA_WIDTH];
    }
    clear_cells((VGA_HEIGHT - 1) * VGA_WIDTH, VGA_HEIGHT * VGA_WIDTH);
}

static void newline(void) {
    if (++row == VGA_HEIGHT) {
        scroll();
        row = VGA_HEIGHT - 1;
    }
}

static void move_cursor(void) {
    uint16_t pos = (uint16_t)(row * VGA_WIDTH + col);

    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)(pos >> 8));
}

static int clamp(int v, int lo, int hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

static void sgr(void) {
    for (int i = 0; i < (nparams ? nparams : 1); i++) {
        int p = params[i];
        if (p == 0) {
            fg = VGA_COLOR_LIGHT_GREY; bg = VGA_COLOR_BLACK;
            bright = reverse = false;
        } else if (p == 1) {
            bright = true;
        } else if (p == 22) {
            bright = false;
        } else if (p == 7) {
            reverse = true;
        } else if (p == 27) {
            reverse = false;
        } else if (p >= 30 && p <= 37) {
            fg = ansi_vga[p - 30];
        } else if (p == 39) {
            fg = VGA_COLOR_LIGHT_GREY;
        } else if (p >= 40 && p <= 47) {
            bg = ansi_vga[p - 40];
        } else if (p == 49) {
            bg = VGA_COLOR_BLACK;
        } else if (p >= 90 && p <= 97) {
            fg = (uint8_t)(ansi_vga[p - 90] | 8);
        }
    }
}

static void csi(char c) {
    int n = nparams > 0 && params[0] > 0 ? params[0] : 1;
    int here = row * VGA_WIDTH + col;
    char buf[24];

    switch (c) {
    case 'A': row = clamp(row - n, 0, VGA_HEIGHT - 1); break;
    case 'B': row = clamp(row + n, 0, VGA_HEIGHT - 1); break;
    case 'C': col = clamp(col + n, 0, VGA_WIDTH - 1); break;
    case 'D': col = clamp(col - n, 0, VGA_WIDTH - 1); break;
    case 'G': col = clamp(n - 1, 0, VGA_WIDTH - 1); break;
    case 'd': row = clamp(n - 1, 0, VGA_HEIGHT - 1); break;
    case 'H':
    case 'f':
        row = clamp(n - 1, 0, VGA_HEIGHT - 1);
        col = clamp((nparams > 1 && params[1] > 0 ? params[1] : 1) - 1, 0, VGA_WIDTH - 1);
        break;
    case 'J':
        if (params[0] == 0) {
            clear_cells(here, VGA_WIDTH * VGA_HEIGHT);
        } else if (params[0] == 1) {
            clear_cells(0, here + 1);
        } else {
            clear_cells(0, VGA_WIDTH * VGA_HEIGHT);
        }
        break;
    case 'K':
        if (params[0] == 0) {
            clear_cells(here, (row + 1) * VGA_WIDTH);
        } else if (params[0] == 1) {
            clear_cells(row * VGA_WIDTH, here + 1);
        } else {
            clear_cells(row * VGA_WIDTH, (row + 1) * VGA_WIDTH);
        }
        break;
    case 'P': /* delete characters */
        for (int i = here; i < (row + 1) * VGA_WIDTH; i++) {
            VGA[i] = i + n < (row + 1) * VGA_WIDTH ? VGA[i + n] : cell(' ');
        }
        break;
    case '@': /* insert blanks */
        for (int i = (row + 1) * VGA_WIDTH - 1; i >= here; i--) {
            VGA[i] = i - n >= here ? VGA[i - n] : cell(' ');
        }
        break;
    case 'm':
        sgr();
        break;
    case 's':
        saved_row = row; saved_col = col;
        break;
    case 'u':
        row = saved_row; col = saved_col;
        break;
    case 'n':
        if (reply != NULL && params[0] == 6) { /* cursor position report */
            char *p = buf;
            *p++ = '\033'; *p++ = '[';
            uitoa((uint64_t)row + 1, p, 10); p += strlen(p);
            *p++ = ';';
            uitoa((uint64_t)col + 1, p, 10); p += strlen(p);
            *p++ = 'R'; *p = '\0';
            reply(buf);
        } else if (reply != NULL && params[0] == 5) { /* status: OK */
            reply("\033[0n");
        }
        break;
    default: /* h/l modes, scroll regions and the rest: ignored */
        break;
    }
    (void)private_seq;
}

void vga_putc(char c) {
    switch (state) {
    case ESC:
        if (c == '[') {
            state = CSI;
            nparams = 0;
            private_seq = false;
            for (int i = 0; i < MAX_PARAMS; i++) {
                params[i] = 0;
            }
        } else {
            if (c == 'c') { /* reset */
                fg = VGA_COLOR_LIGHT_GREY; bg = VGA_COLOR_BLACK;
                bright = reverse = false;
                clear_cells(0, VGA_WIDTH * VGA_HEIGHT);
                row = col = 0;
            }
            state = NORMAL;
        }
        move_cursor();
        return;
    case CSI:
        if (c >= '0' && c <= '9') {
            if (nparams == 0) {
                nparams = 1;
            }
            params[nparams - 1] = params[nparams - 1] * 10 + (c - '0');
        } else if (c == ';') {
            if (nparams == 0) {
                nparams = 1;
            }
            if (nparams < MAX_PARAMS) {
                nparams++;
            }
        } else if (c == '?') {
            private_seq = true;
        } else if (c >= 0x40 && c <= 0x7E) {
            csi(c);
            state = NORMAL;
        }
        move_cursor();
        return;
    case NORMAL:
        break;
    }

    switch (c) {
    case '\033':
        state = ESC;
        return;
    case '\n':
        newline();
        break;
    case '\r':
        col = 0;
        break;
    case '\b':
        if (col > 0) {
            col--;
        }
        break;
    case '\t':
        col = (col + 8) & ~7;
        if (col >= VGA_WIDTH) {
            col = 0;
            newline();
        }
        break;
    case '\a':
        break;
    default:
        if ((unsigned char)c < 32) {
            break;
        }
        if (col == VGA_WIDTH) {
            col = 0;
            newline();
        }
        VGA[row * VGA_WIDTH + col] = cell(c);
        col++;
        break;
    }
    move_cursor();
}

void vga_set_reply(void (*fn)(const char *)) {
    reply = fn;
}

/* Kernel messages: both the screen and the serial port, "\n" as CRLF. */
void terminal_putchar(char c) {
    if (c == '\n') {
        serial_write('\r');
        vga_putc('\r');
    }
    serial_write(c);
    vga_putc(c);
}

void terminal_clear(void) {
    clear_cells(0, VGA_WIDTH * VGA_HEIGHT);
    row = col = 0;
    move_cursor();
}

void terminal_print(const char *str) {
    while (*str) {
        terminal_putchar(*str++);
    }
}

void terminal_print_colorful(const char *str, unsigned char color) {
    uint8_t f = fg, b = bg;
    bool br = bright;

    fg = color & 7;
    bright = color & 8;
    bg = (color >> 4) & 7;
    terminal_print(str);
    fg = f; bg = b; bright = br;
}

void terminal_print_num(uint64_t num) {
    char buf[50];
    uitoa(num, buf, 10);
    terminal_print(buf);
}

void terminal_print_hex(uint32_t num) {
    char buf[50];
    uitoa(num, buf, 16);
    terminal_print(buf);
}
