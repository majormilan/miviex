/*
 * Placeholder init: keeps /bin/sh running on the console and reaps every
 * orphan it inherits. To be replaced by a real init system.
 */
#include <stdio.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/wait.h>

static char *const shell[] = { "/bin/sh", NULL };

/* Like getty: a new session with the console as controlling terminal, so
 * ^C reaches the shell's foreground job and not init. */
static void
spawn(void)
{
	setsid();
	ioctl(0, TIOCSCTTY, 1);
	execv(shell[0], shell);
	perror("init: exec /bin/sh");
	_exit(127);
}

int
main(void)
{
	char c;
	pid_t pid;
	int st;

	for (;;) {
		if ((pid = fork()) < 0) {
			perror("init: fork");
			return 1;
		}
		if (pid == 0)
			spawn();
		while (wait(&st) != pid)
			; /* reaps adopted orphans too */
		if (WIFEXITED(st) && WEXITSTATUS(st) == 127) {
			fputs("init: no usable shell, press enter to retry\n", stderr);
			while (read(0, &c, 1) == 1 && c != '\n')
				;
		} else {
			fprintf(stderr, "init: shell exited (status %d), restarting\n", st);
		}
	}
}
