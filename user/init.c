/*
 * Placeholder init: mounts /proc and /run, sets the hostname, keeps a getty
 * on every terminal and reaps every orphan it inherits. To be replaced by
 * a real init system.
 */
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/mount.h>
#include <sys/wait.h>

static const struct {
	char *tty, *term;
} ttys[] = {
	{ "/dev/tty1", "linux" },
	{ "/dev/ttyS0", "vt100" },
};

#define NTTY (sizeof(ttys) / sizeof(ttys[0]))

static pid_t pids[NTTY];
static time_t started[NTTY];

static void
set_hostname(void)
{
	char name[65];
	FILE *f;

	if (!(f = fopen("/etc/hostname", "r")))
		return;
	if (fgets(name, sizeof(name), f)) {
		name[strcspn(name, "\n")] = '\0';
		sethostname(name, strlen(name));
	}
	fclose(f);
}

/* getty makes its own session and takes the terminal. A getty that dies
 * at once (no device, no binary) is not restarted in a tight loop. */
static void
spawn(size_t i)
{
	if (started[i] && time(NULL) - started[i] < 1)
		sleep(1);
	started[i] = time(NULL);
	if ((pids[i] = fork()) < 0) {
		perror("init: fork");
		pids[i] = 0;
		return;
	}
	if (pids[i] == 0) {
		execl("/bin/getty", "getty", ttys[i].tty, ttys[i].term, (char *)0);
		perror("init: exec /bin/getty");
		_exit(127);
	}
}

int
main(void)
{
	pid_t pid;
	size_t i;

	mount("proc", "/proc", "proc", 0, NULL);
	mount("tmpfs", "/run", "tmpfs", 0, "mode=0755");
	set_hostname();
	for (i = 0; i < NTTY; i++)
		spawn(i);
	for (;;) {
		if ((pid = wait(NULL)) < 0)
			continue;
		for (i = 0; i < NTTY; i++)
			if (pids[i] == pid)
				spawn(i); /* other pids are adopted orphans: just reaped */
	}
}
