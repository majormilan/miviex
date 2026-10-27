/*
 * fb.c - a linear framebuffer for the console, from the display adapter
 * Bochs and QEMU emulate (and VirtualBox's): "BGA", set up through the
 * VBE DISPI registers behind I/O ports 0x1CE/0x1CF, its memory in BAR 0.
 * No BIOS calls, so it works the same booted from BIOS or (later) UEFI.
 *
 * The font is the VGA card's own, read from plane 2 while the card is
 * still in text mode: 8x16 glyphs in code page 437.
 *
 * The kernel command line chooses: vga=1280x800 for a size, vga=text to
 * keep the text mode. 1024x768 otherwise, 32 bits per pixel.
 */
#include <kernel/video/fb.h>
#include <kernel/video/vga.h>
#include <kernel/drivers/pci.h>
#include <kernel/hal/io.h>
#include <kernel/libc/string.h>
#include <kernel/log.h>
#include <kernel/mm/memory.h>
#include <kernel/mm/vmm.h>

#define DISPI_INDEX 0x1CE
#define DISPI_DATA  0x1CF
enum { DISPI_ID, DISPI_XRES, DISPI_YRES, DISPI_BPP, DISPI_ENABLE };
#define DISPI_ENABLED 0x01
#define DISPI_LFB     0x40

#define GLYPH_W 8
#define GLYPH_H 16
#define DEFAULT_W 1024
#define DEFAULT_H 768

static uint8_t font[256][GLYPH_H];
static volatile uint32_t *fb;
static uint32_t width, height, pitch; /* pitch in pixels */
static screen_t screen;

static void dispi_set(uint16_t reg, uint16_t v) {
    outw(DISPI_INDEX, reg);
    outw(DISPI_DATA, v);
}

static uint16_t dispi_get(uint16_t reg) {
    outw(DISPI_INDEX, reg);
    return inw(DISPI_DATA);
}

/* Plane 2 holds the glyphs, 32 bytes apart. Map it alone at 0xA0000,
 * copy, and put the card back as text mode wants it. */
static void read_font(void) {
    volatile uint8_t *vram = (volatile uint8_t *)(0xA0000 + KERNEL_BASE);

    outw(0x3C4, 0x0402); /* sequencer map mask: plane 2 */
    outw(0x3C4, 0x0704); /* sequential addressing */
    outw(0x3CE, 0x0204); /* read plane 2 */
    outw(0x3CE, 0x0005); /* no odd/even */
    outw(0x3CE, 0x0406); /* at 0xA0000, 64 KiB */
    for (int c = 0; c < 256; c++) {
        for (int y = 0; y < GLYPH_H; y++) {
            font[c][y] = vram[c * 32 + y];
        }
    }
    outw(0x3C4, 0x0302); /* planes 0 and 1 */
    outw(0x3C4, 0x0304); /* odd/even */
    outw(0x3CE, 0x0004);
    outw(0x3CE, 0x1005);
    outw(0x3CE, 0x0E06); /* at 0xB8000 */
}

static void draw(int col, int row, const vcell_t *c, bool cursor) {
    uint32_t fg = cursor ? c->bg : c->fg, bg = cursor ? c->fg : c->bg;
    volatile uint32_t *p = fb + (uint32_t)row * GLYPH_H * pitch + (uint32_t)col * GLYPH_W;
    const uint8_t *g = font[c->ch & 0xFF];

    for (int y = 0; y < GLYPH_H; y++, p += pitch) {
        uint8_t bits = g[y];
        for (int x = 0; x < GLYPH_W; x++) {
            p[x] = bits & (0x80 >> x) ? fg : bg;
        }
    }
}

/* "vga=WxH" or "vga=text" on the command line */
static int wanted(const char *cmdline, uint32_t *w, uint32_t *h) {
    const char *v = cmdline != NULL ? strstr(cmdline, "vga=") : NULL;
    uint32_t a = 0, b = 0;

    *w = DEFAULT_W;
    *h = DEFAULT_H;
    if (v == NULL) {
        return 0;
    }
    v += 4;
    if (strncmp(v, "text", 4) == 0) {
        return -1;
    }
    while (*v >= '0' && *v <= '9') {
        a = a * 10 + (uint32_t)(*v++ - '0');
    }
    if (*v++ == 'x') {
        while (*v >= '0' && *v <= '9') {
            b = b * 10 + (uint32_t)(*v++ - '0');
        }
    }
    if (a >= 320 && b >= 200 && a <= 4096 && b <= 4096) {
        *w = a & ~7u;
        *h = b;
    }
    return 0;
}

int fb_init(const char *cmdline) {
    pci_dev_t *d = pci_find_id(NULL, 0x1234, 0x1111);  /* Bochs, QEMU std */
    uint16_t id;
    uint32_t w, h;

    if (d == NULL) {
        d = pci_find_id(NULL, 0x80EE, 0xBEEF);          /* VirtualBox */
    }
    if (d == NULL || wanted(cmdline, &w, &h) < 0) {
        return -1;
    }
    id = dispi_get(DISPI_ID);
    if (id < 0xB0C0 || id > 0xB0CF || d->bar[0].addr == 0 || d->bar[0].io) {
        klog(LOG_INFO, "fb", "no usable BGA (id 0x%x)", id);
        return -1;
    }
    if ((uint64_t)w * h * 4 > d->bar[0].size) {
        klog(LOG_FAIL, "fb", "%ux%u does not fit in %u KiB of video memory",
             w, h, (uint32_t)(d->bar[0].size / 1024));
        return -1;
    }
    read_font();
    pci_enable(d, false);
    dispi_set(DISPI_ENABLE, 0);
    dispi_set(DISPI_XRES, (uint16_t)w);
    dispi_set(DISPI_YRES, (uint16_t)h);
    dispi_set(DISPI_BPP, 32);
    dispi_set(DISPI_ENABLE, DISPI_ENABLED | DISPI_LFB);
    width = dispi_get(DISPI_XRES); /* what the card took */
    height = dispi_get(DISPI_YRES);
    pitch = width;
    fb = vmm_ioremap(d->bar[0].addr, (size_t)pitch * height * 4, true);
    if (fb == NULL) {
        dispi_set(DISPI_ENABLE, 0);
        return -1;
    }
    screen = (screen_t){ (int)(width / GLYPH_W), (int)(height / GLYPH_H), draw };
    if (vga_set_screen(&screen) < 0) {
        dispi_set(DISPI_ENABLE, 0);
        return -1;
    }
    klog(LOG_OK, "fb", "%ux%u, 32 bpp at 0x%x: %d x %d characters", width, height,
         (uint32_t)d->bar[0].addr, screen.cols, screen.rows);
    return 0;
}
