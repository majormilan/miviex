/*  vga.c - the console's terminal emulator (VT100/ANSI, with 256 and
 *  24-bit colors, UTF-8) on a screen of character cells. The screen is
 *  the VGA text mode until fb.c finds a framebuffer and takes over. Cells
 *  hold Unicode characters; only the text mode turns them into its
 *  code page 437.
 *
 *  The emulator keeps two grids: what the cells should show and what the
 *  screen shows. Only cells that differ are drawn, so scrolling costs
 *  the lines that changed, never a read of video memory.
 */
#include <kernel/video/vga.h>
#include <kernel/libc/stdlib.h>
#include <kernel/libc/string.h>
#include <kernel/mm/memory.h>
#include <kernel/types.h>
#include <kernel/hal/io.h>

#define VGA_ADDRESS 0xB8000
#define TEXT_COLS 80
#define TEXT_ROWS 25
#define MAX_PARAMS 16

static volatile uint16_t *const VGA = (uint16_t *)(VGA_ADDRESS + KERNEL_BASE);

/* The 16 ANSI colors, as the VGA palette has them */
static const uint32_t ansi[16] = {
    0x000000, 0xAA0000, 0x00AA00, 0xAA5500, 0x0000AA, 0xAA00AA, 0x00AAAA, 0xAAAAAA,
    0x555555, 0xFF5555, 0x55FF55, 0xFFFF55, 0x5555FF, 0xFF55FF, 0x55FFFF, 0xFFFFFF,
};
/* ANSI color index -> VGA text attribute color */
static const uint8_t ansi_vga[8] = { 0, 4, 2, 6, 1, 5, 3, 7 };

#define DEFAULT_FG 7
#define DEFAULT_BG 0

static void text_draw(int col, int row, const vcell_t *c, bool cursor);
static uint8_t glyph(uint32_t u);
static const screen_t text_screen = { TEXT_COLS, TEXT_ROWS, text_draw, NULL };
static const screen_t *scr = &text_screen;

/* Before there is a heap: the text mode's grids */
static vcell_t text_want[TEXT_COLS * TEXT_ROWS], text_shown[TEXT_COLS * TEXT_ROWS];
static vcell_t *want = text_want, *shown = text_shown;
static int cols = TEXT_COLS, rows = TEXT_ROWS;

static int row, col, saved_row, saved_col;
static int fgi = DEFAULT_FG, bgi = DEFAULT_BG; /* palette index, or -1: fgc/bgc */
static uint32_t fgc, bgc;
static bool bold, reverse, cursor_on = true;
static int cur_row = -1, cur_col = -1;          /* where the cursor is drawn */
static enum { NORMAL, ESC, CSI } state;
static int params[MAX_PARAMS], nparams;
static bool private_seq;
static uint32_t utf8, utf8_left;                /* a UTF-8 sequence so far */
static void (*reply)(const char *);

/* xterm's 256 colors: 16 ANSI, a 6x6x6 cube, 24 grays */
static uint32_t palette(int i) {
    static const uint8_t level[6] = { 0, 95, 135, 175, 215, 255 };

    if (i < 16) {
        return ansi[i];
    }
    if (i < 232) {
        i -= 16;
        return (uint32_t)level[i / 36] << 16 | (uint32_t)level[i / 6 % 6] << 8 | level[i % 6];
    }
    i = 8 + (i - 232) * 10;
    return (uint32_t)i << 16 | (uint32_t)i << 8 | (uint32_t)i;
}

/* The nearest of the 16 colors, for the text mode */
static uint8_t nearest16(uint32_t rgb) {
    int best = 0;
    long bestd = -1;

    for (int i = 0; i < 16; i++) {
        long dr = (long)(rgb >> 16 & 0xFF) - (long)(ansi[i] >> 16 & 0xFF);
        long dg = (long)(rgb >> 8 & 0xFF) - (long)(ansi[i] >> 8 & 0xFF);
        long db = (long)(rgb & 0xFF) - (long)(ansi[i] & 0xFF);
        long d = dr * dr + dg * dg + db * db;
        if (bestd < 0 || d < bestd) {
            best = i;
            bestd = d;
        }
    }
    return (uint8_t)(ansi_vga[best & 7] | (best & 8));
}

static void text_draw(int c, int r, const vcell_t *v, bool cursor) {
    (void)cursor; /* the hardware draws it */
    uint8_t g = glyph(v->ch == VCELL_TAIL ? ' ' : v->ch);

    VGA[r * TEXT_COLS + c] = (uint16_t)(g | (nearest16(v->fg) | nearest16(v->bg) << 4) << 8);
}

static void move_hw_cursor(void) {
    uint16_t pos = (uint16_t)(row * TEXT_COLS + (col < cols ? col : cols - 1));

    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)(pos >> 8));
}

/* The colors a new cell gets */
static vcell_t blank(uint32_t ch) {
    uint32_t f = fgi >= 0 ? palette(bold && fgi < 8 ? fgi + 8 : fgi) : fgc;
    uint32_t b = bgi >= 0 ? palette(bgi) : bgc;
    vcell_t v = { ch, f, b };

    if (reverse) {
        v.fg = b;
        v.bg = f;
    }
    return v;
}

static bool same(const vcell_t *a, const vcell_t *b) {
    return a->ch == b->ch && a->fg == b->fg && a->bg == b->bg;
}

static bool wide(uint32_t ch) {
    return scr->cells != NULL && ch != VCELL_TAIL && scr->cells(ch) == 2;
}

/* Draw what differs. A wide character's head draws both its cells; a
 * tail without a wide head before it is drawn as a blank. */
static void refresh(int from, int to) {
    for (int i = from; i < to; i++) {
        if (same(&want[i], &shown[i])) {
            continue;
        }
        shown[i] = want[i];
        if (want[i].ch != VCELL_TAIL) {
            scr->draw(i % cols, i / cols, &want[i], false);
        } else if (i % cols == 0 || !wide(want[i - 1].ch)) {
            vcell_t b = want[i];
            b.ch = ' ';
            scr->draw(i % cols, i / cols, &b, false);
        } else {
            scr->draw((i - 1) % cols, i / cols, &want[i - 1], false);
        }
    }
}

static void place_cursor(void) {
    int r = row, c = col < cols ? col : cols - 1;

    if (scr == &text_screen) {
        move_hw_cursor();
        return;
    }
    if (cur_row >= 0 && (cur_row != r || cur_col != c || !cursor_on)) {
        scr->draw(cur_col, cur_row, &shown[cur_row * cols + cur_col], false);
        cur_row = -1;
    }
    if (cursor_on && cur_row < 0) {
        scr->draw(c, r, &shown[r * cols + c], true);
        cur_row = r;
        cur_col = c;
    }
}

/* Take the cursor off the screen (its cell drawn plain): before cells
 * change under it */
static void uncursor(void) {
    if (cur_row >= 0) {
        scr->draw(cur_col, cur_row, &shown[cur_row * cols + cur_col], false);
        cur_row = -1;
    }
}

/* Writing over half of a wide character blanks the other half. */
static void put(int i, vcell_t v) {
    int from = i, to = i + 1;

    if (i == cur_row * cols + cur_col) {
        uncursor(); /* drawn over: the cursor goes back on after */
    }
    if (want[i].ch == VCELL_TAIL && v.ch != VCELL_TAIL && i % cols > 0) {
        want[i - 1] = blank(' ');
        from = i - 1;
    }
    if (wide(want[i].ch) && !wide(v.ch) && (i + 1) % cols != 0 && want[i + 1].ch == VCELL_TAIL) {
        want[i + 1] = blank(' ');
        to = i + 2;
    }
    want[i] = v;
    refresh(from, to);
}

static void clear_cells(int from, int to) {
    vcell_t v = blank(' ');

    for (int i = from; i < to; i++) {
        want[i] = v;
    }
    if (cur_row >= 0 && cur_row * cols + cur_col >= from && cur_row * cols + cur_col < to) {
        uncursor();
    }
    refresh(from, to);
}

/* Lines top..bottom-1 move up (n > 0) or down (n < 0) */
static void shift_lines(int top, int bottom, int n) {
    int len = (bottom - top) * cols;

    if (n > 0) {
        uncursor();
        memmove(&want[top * cols], &want[(top + n) * cols], (size_t)(len - n * cols) * sizeof(*want));
        clear_cells((bottom - n) * cols, bottom * cols);
    } else if (n < 0) {
        n = -n;
        uncursor();
        memmove(&want[(top + n) * cols], &want[top * cols], (size_t)(len - n * cols) * sizeof(*want));
        clear_cells(top * cols, (top + n) * cols);
    }
    refresh(top * cols, bottom * cols);
}

static void newline(void) {
    if (++row == rows) {
        shift_lines(0, rows, 1);
        row = rows - 1;
    }
}

static int clamp(int v, int lo, int hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

/* 38;5;n and 38;2;r;g;b (48 for the background): how many it used */
static int extended(int i, int *idx, uint32_t *rgb) {
    if (i + 2 < nparams && params[i + 1] == 5) {
        *idx = clamp(params[i + 2], 0, 255);
        return 2;
    }
    if (i + 4 < nparams && params[i + 1] == 2) {
        *idx = -1;
        *rgb = (uint32_t)(params[i + 2] & 0xFF) << 16 | (uint32_t)(params[i + 3] & 0xFF) << 8 |
               (uint32_t)(params[i + 4] & 0xFF);
        return 4;
    }
    return 0;
}

static void sgr(void) {
    for (int i = 0; i < (nparams ? nparams : 1); i++) {
        int p = params[i];
        if (p == 0) {
            fgi = DEFAULT_FG;
            bgi = DEFAULT_BG;
            bold = reverse = false;
        } else if (p == 1) {
            bold = true;
        } else if (p == 22) {
            bold = false;
        } else if (p == 7) {
            reverse = true;
        } else if (p == 27) {
            reverse = false;
        } else if (p >= 30 && p <= 37) {
            fgi = p - 30;
        } else if (p == 38) {
            i += extended(i, &fgi, &fgc);
        } else if (p == 39) {
            fgi = DEFAULT_FG;
        } else if (p >= 40 && p <= 47) {
            bgi = p - 40;
        } else if (p == 48) {
            i += extended(i, &bgi, &bgc);
        } else if (p == 49) {
            bgi = DEFAULT_BG;
        } else if (p >= 90 && p <= 97) {
            fgi = p - 90 + 8;
        } else if (p >= 100 && p <= 107) {
            bgi = p - 100 + 8;
        }
    }
}

static void csi(char c) {
    int n = nparams > 0 && params[0] > 0 ? params[0] : 1;
    int here = row * cols + (col < cols ? col : cols - 1);
    char buf[24];

    if (private_seq) {
        if (params[0] == 25 && (c == 'h' || c == 'l')) {
            cursor_on = c == 'h'; /* show/hide the cursor */
        }
        return;
    }
    switch (c) {
    case 'A': row = clamp(row - n, 0, rows - 1); break;
    case 'B': row = clamp(row + n, 0, rows - 1); break;
    case 'C': col = clamp(col + n, 0, cols - 1); break;
    case 'D': col = clamp(col - n, 0, cols - 1); break;
    case 'G': col = clamp(n - 1, 0, cols - 1); break;
    case 'd': row = clamp(n - 1, 0, rows - 1); break;
    case 'H':
    case 'f':
        row = clamp(n - 1, 0, rows - 1);
        col = clamp((nparams > 1 && params[1] > 0 ? params[1] : 1) - 1, 0, cols - 1);
        break;
    case 'J':
        if (params[0] == 0) {
            clear_cells(here, cols * rows);
        } else if (params[0] == 1) {
            clear_cells(0, here + 1);
        } else {
            clear_cells(0, cols * rows);
        }
        break;
    case 'K':
        if (params[0] == 0) {
            clear_cells(here, (row + 1) * cols);
        } else if (params[0] == 1) {
            clear_cells(row * cols, here + 1);
        } else {
            clear_cells(row * cols, (row + 1) * cols);
        }
        break;
    case 'X': /* erase characters */
        clear_cells(here, here + n < (row + 1) * cols ? here + n : (row + 1) * cols);
        break;
    case 'P': /* delete characters */
        uncursor();
        for (int i = here; i < (row + 1) * cols; i++) {
            want[i] = i + n < (row + 1) * cols ? want[i + n] : blank(' ');
        }
        refresh(here, (row + 1) * cols);
        break;
    case '@': /* insert blanks */
        uncursor();
        for (int i = (row + 1) * cols - 1; i >= here; i--) {
            want[i] = i - n >= here ? want[i - n] : blank(' ');
        }
        refresh(here, (row + 1) * cols);
        break;
    case 'L': /* insert lines */
        shift_lines(row, rows, -clamp(n, 1, rows - row));
        break;
    case 'M': /* delete lines */
        shift_lines(row, rows, clamp(n, 1, rows - row));
        break;
    case 'S': /* scroll up */
        shift_lines(0, rows, clamp(n, 1, rows));
        break;
    case 'T': /* scroll down */
        shift_lines(0, rows, -clamp(n, 1, rows));
        break;
    case 'm':
        sgr();
        break;
    case 's':
        saved_row = row; saved_col = col;
        break;
    case 'u':
        row = clamp(saved_row, 0, rows - 1); col = clamp(saved_col, 0, cols - 1);
        break;
    case 'n':
        if (reply != NULL && params[0] == 6) { /* cursor position report */
            char *p = buf;
            *p++ = '\033'; *p++ = '[';
            uitoa((uint64_t)row + 1, p, 10); p += strlen(p);
            *p++ = ';';
            uitoa((uint64_t)(col < cols ? col : cols - 1) + 1, p, 10); p += strlen(p);
            *p++ = 'R'; *p = '\0';
            reply(buf);
        } else if (reply != NULL && params[0] == 5) { /* status: OK */
            reply("\033[0n");
        }
        break;
    default: /* other modes and scroll regions: ignored */
        break;
    }
}

/* Code page 437, the font's encoding, from 0x80 up */
static const uint16_t cp437[128] = {
    0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7, 0x00EA, 0x00EB, 0x00E8, 0x00EF,
    0x00EE, 0x00EC, 0x00C4, 0x00C5, 0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9,
    0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192, 0x00E1, 0x00ED, 0x00F3, 0x00FA,
    0x00F1, 0x00D1, 0x00AA, 0x00BA, 0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB,
    0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556, 0x2555, 0x2563, 0x2551, 0x2557,
    0x255D, 0x255C, 0x255B, 0x2510, 0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F,
    0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567, 0x2568, 0x2564, 0x2565, 0x2559,
    0x2558, 0x2552, 0x2553, 0x256B, 0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580,
    0x03B1, 0x00DF, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4, 0x03A6, 0x0398, 0x03A9, 0x03B4,
    0x221E, 0x03C6, 0x03B5, 0x2229, 0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248,
    0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0,
};

/* A character to a glyph of the VGA text mode's font; letters it lacks
 * get a close one. */
static uint8_t glyph(uint32_t u) {
    if (u < 0x80) {
        return (uint8_t)u;
    }
    for (int i = 0; i < 128; i++) {
        if (cp437[i] == u) {
            return (uint8_t)(0x80 + i);
        }
    }
    switch (u) {
    case 0x0151: return 0x94; /* ő as ö */
    case 0x0171: return 0x81; /* ű as ü */
    case 0x0150: return 0x99; /* Ő as Ö */
    case 0x0170: return 0x9A; /* Ű as Ü */
    case 0x00C1: return 'A';  /* Á É Í Ó Ú: no capitals with acute */
    case 0x00CD: return 'I';
    case 0x00D3: return 'O';
    case 0x00DA: return 'U';
    }
    return '?';
}

static void print(uint32_t u) {
    int w = scr->cells != NULL ? scr->cells(u) : 1;
    vcell_t v = blank(u);

    if (col + w > cols) {
        col = 0;
        newline();
    }
    put(row * cols + col, v);
    if (w == 2) {
        v.ch = VCELL_TAIL;
        put(row * cols + col + 1, v);
    }
    col += w;
}

static void putc_now(char ch) {
    unsigned char c = (unsigned char)ch;

    switch (state) {
    case ESC:
        if (c == '[') {
            state = CSI;
            nparams = 0;
            private_seq = false;
            memset(params, 0, sizeof(params));
            return;
        }
        if (c == 'c') { /* reset */
            fgi = DEFAULT_FG;
            bgi = DEFAULT_BG;
            bold = reverse = false;
            cursor_on = true;
            clear_cells(0, cols * rows);
            row = col = 0;
        } else if (c == 'M') { /* reverse index */
            if (row == 0) {
                shift_lines(0, rows, -1);
            } else {
                row--;
            }
        }
        state = NORMAL;
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
            csi((char)c);
            state = NORMAL;
        }
        return;
    case NORMAL:
        break;
    }

    if (utf8_left > 0) {
        if ((c & 0xC0) == 0x80) {
            utf8 = utf8 << 6 | (c & 0x3F);
            if (--utf8_left == 0) {
                print(utf8);
            }
            return;
        }
        utf8_left = 0;
        print(0xFFFD); /* cut short: the byte starts something new */
    }
    if (c >= 0xC0 && c < 0xF8) {
        utf8_left = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1;
        utf8 = c & (0x3F >> utf8_left);
        return;
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
        if (col >= cols) {
            col = 0;
            newline();
        }
        break;
    default:
        if (c >= 32) {
            print(c < 0x80 ? c : 0xFFFD); /* a stray byte of UTF-8 */
        }
        break;
    }
}

void vga_putc(char c) {
    putc_now(c);
    if (state == NORMAL) {
        place_cursor();
    }
}

void vga_set_reply(void (*fn)(const char *)) {
    reply = fn;
}

void vga_size(int *r, int *c) {
    *r = rows;
    *c = cols;
}

/* A new screen: what was on the old one stays, top left, and the rest is
 * blank. Grids for anything larger than the text mode come from the heap. */
int vga_set_screen(const screen_t *s) {
    size_t n = (size_t)s->cols * (size_t)s->rows;
    vcell_t *w, *sh;
    vcell_t empty = blank(' ');

    if ((w = k_malloc(n * sizeof(*w))) == NULL || (sh = k_malloc(n * sizeof(*sh))) == NULL) {
        k_free(w);
        return -1;
    }
    for (size_t i = 0; i < n; i++) {
        int r = (int)(i / (size_t)s->cols), c = (int)(i % (size_t)s->cols);
        w[i] = r < rows && c < cols ? want[r * cols + c] : empty;
        sh[i] = (vcell_t){ 0, 1, 1 }; /* matches nothing: all is drawn */
    }
    if (want != text_want) {
        k_free(want);
        k_free(shown);
    }
    want = w;
    shown = sh;
    scr = s;
    cols = s->cols;
    rows = s->rows;
    row = clamp(row, 0, rows - 1);
    col = clamp(col, 0, cols - 1);
    cur_row = -1;
    refresh(0, cols * rows);
    place_cursor();
    return 0;
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
    clear_cells(0, cols * rows);
    row = col = 0;
    place_cursor();
}

void terminal_print(const char *str) {
    while (*str) {
        terminal_putchar(*str++);
    }
}

void terminal_print_colorful(const char *str, unsigned char color) {
    int f = fgi, b = bgi;
    bool br = bold;

    fgi = color & 7;
    bold = color & 8;
    bgi = (color >> 4) & 7;
    terminal_print(str);
    fgi = f; bgi = b; bold = br;
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
