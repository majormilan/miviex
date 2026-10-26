/*
 * Placeholder init: keeps /bin/sh running on the console and reaps every
 * orphan it inherits. To be replaced by a real init system.
 */
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>

static char *const shell[] = { "/bin/sh", NULL };

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
		if (pid == 0) {
			execv(shell[0], shell);
			perror("init: exec /bin/sh");
			_exit(127);
		}
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
