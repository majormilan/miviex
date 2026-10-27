/* fb.h - the console on a linear framebuffer */
#ifndef FB_H
#define FB_H

/* Find the display adapter and switch the console to it: 0, or -1 when
 * there is none (or vga=text) and the text mode stays. */
int fb_init(const char *cmdline);

#endif /* FB_H */
