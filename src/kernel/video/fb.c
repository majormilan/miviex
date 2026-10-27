/*
 * fb.c - a linear framebuffer for the console, from the display adapter
 * Bochs and QEMU emulate (and VirtualBox's): "BGA", set up through the
 * VBE DISPI registers behind I/O ports 0x1CE/0x1CF, its memory in BAR 0.
 * No BIOS calls, so it works the same booted from BIOS or (later) UEFI.
 *
 * Glyphs come from libfnt (ext/libfnt), compiled in: unscii-16 (8x16)
 * and the misc-fixed 10x20, both public domain, with Latin-1 and Latin
 * Extended-A.
 *
 * The kernel command line chooses: vga=1280x800 for a size, vga=text to
 * keep the text mode (1024x768 otherwise, 32 bits per pixel); font=10x20
 * or font=unscii-16 (10x20 from 1600 pixels wide on, otherwise 8x16).
 */
#include <kernel/video/fb.h>
#include <kernel/video/vga.h>
#include <kernel/drivers/pci.h>
#include <kernel/hal/io.h>
#include <kernel/libc/string.h>
#include <kernel/log.h>
#include <kernel/mm/memory.h>
#include <kernel/mm/vmm.h>
#include <fnt.h>

#define DISPI_INDEX 0x1CE
#define DISPI_DATA  0x1CF
enum { DISPI_ID, DISPI_XRES, DISPI_YRES, DISPI_BPP, DISPI_ENABLE };
#define DISPI_ENABLED 0x01
#define DISPI_LFB     0x40

#define DEFAULT_W 1024
#define DEFAULT_H 768
#define BIG_FONT_W 1600 /* and wider: the 10x20 font */

extern const Fnt fnt_unscii_16, fnt_10x20;

static uint32_t *fb;
static uint32_t width, height, pitch; /* pitch in pixels */
static const Fnt *font;
static screen_t screen;

static void dispi_set(uint16_t reg, uint16_t v) {
    outw(DISPI_INDEX, reg);
    outw(DISPI_DATA, v);
}

static uint16_t dispi_get(uint16_t reg) {
    outw(DISPI_INDEX, reg);
    return inw(DISPI_DATA);
}

static void draw(int col, int row, const vcell_t *c, bool cursor) {
    uint32_t *at = fb + (uint32_t)row * (uint32_t)font->h * pitch + (uint32_t)col * (uint32_t)font->w;

    fntdraw(font, c->ch, at, pitch, cursor ? c->bg : c->fg, cursor ? c->fg : c->bg);
}

static int cells(uint32_t ch) {
    return fntcells(font, ch);
}

static const char *option(const char *cmdline, const char *name) {
    const char *v = cmdline != NULL ? strstr(cmdline, name) : NULL;

    return v != NULL ? v + strlen(name) : NULL;
}

/* "vga=WxH" or "vga=text" on the command line */
static int wanted(const char *cmdline, uint32_t *w, uint32_t *h) {
    const char *v = option(cmdline, "vga=");
    uint32_t a = 0, b = 0;

    *w = DEFAULT_W;
    *h = DEFAULT_H;
    if (v == NULL) {
        return 0;
    }
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

static const Fnt *pick_font(const char *cmdline, uint32_t w) {
    const char *v = option(cmdline, "font=");

    if (v != NULL && strncmp(v, "10x20", 5) == 0) {
        return &fnt_10x20;
    }
    if (v != NULL && strncmp(v, "unscii-16", 9) == 0) {
        return &fnt_unscii_16;
    }
    return w >= BIG_FONT_W ? &fnt_10x20 : &fnt_unscii_16;
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
    font = pick_font(cmdline, width);
    screen = (screen_t){ (int)(width / (uint32_t)font->w), (int)(height / (uint32_t)font->h),
                         draw, cells };
    memset(fb, 0, (size_t)pitch * height * 4); /* the margin right and below */
    if (vga_set_screen(&screen) < 0) {
        dispi_set(DISPI_ENABLE, 0);
        return -1;
    }
    klog(LOG_OK, "fb", "%ux%u, 32 bpp at 0x%x, font %s: %d x %d characters", width, height,
         (uint32_t)d->bar[0].addr, font->name, screen.cols, screen.rows);
    return 0;
}
