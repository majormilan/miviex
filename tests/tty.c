/* Terminals: /dev/tty without one, stealing one, window sizes */
#include <fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>

#include "t.h"

static int
devtty_without_ctty(void)
{
	setsid();
	return !(open("/dev/tty", O_RDWR) < 0 && errno == ENXIO);
}

/* ttyS0 is the console here, held by its getty: only TIOCSCTTY with
 * arg 1 may take it away, and only for root */
static int
steal_needs_force(void)
{
	int fd;

	setsid();
	if ((fd = open("/dev/ttyS0", O_RDWR | O_NOCTTY)) < 0)
		return 2;
	return !(ioctl(fd, TIOCSCTTY, 0) < 0 && errno == EPERM);
}

static int
steal_needs_root(void)
{
	int fd;

	if ((fd = open("/dev/ttyS0", O_RDWR | O_NOCTTY)) < 0)
		return 2;
	if (setgid(1000) < 0 || setuid(1000) < 0)
		return 2;
	setsid();
	return !(ioctl(fd, TIOCSCTTY, 1) < 0 && errno == EPERM);
}

int
main(void)
{
	struct winsize old, ws = { .ws_row = 50, .ws_col = 132 }, got;
	int fd, r;

	r = within(2000, devtty_without_ctty);
	check(r == 0, "/dev/tty without a controlling terminal is ENXIO (got %d)", r);
	r = within(2000, steal_needs_force);
	check(r == 0, "TIOCSCTTY 0 does not steal a terminal (got %d)", r);
	r = within(2000, steal_needs_root);
	check(r == 0, "TIOCSCTTY 1 steals only for root (got %d)", r);

	if ((fd = open("/dev/ttyS0", O_RDWR | O_NOCTTY)) >= 0) {
		ioctl(fd, TIOCGWINSZ, &old);
		ioctl(fd, TIOCSWINSZ, &ws);
		memset(&got, 0, sizeof(got));
		ioctl(fd, TIOCGWINSZ, &got);
		check(got.ws_row == 50 && got.ws_col == 132,
		      "TIOCSWINSZ sets the size TIOCGWINSZ reports (%dx%d)", got.ws_col, got.ws_row);
		ioctl(fd, TIOCSWINSZ, &old);
		close(fd);
	}
	/* the framebuffer console (QEMU's std VGA): 1024x768, 8x16 glyphs */
	if ((fd = open("/dev/tty1", O_RDWR | O_NOCTTY)) >= 0) {
		ioctl(fd, TIOCGWINSZ, &got);
		check(got.ws_col == 128 && got.ws_row == 48,
		      "the console runs on the framebuffer: /dev/tty1 is %dx%d", got.ws_col, got.ws_row);
		close(fd);
	}
	return done();
}
