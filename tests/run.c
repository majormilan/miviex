/* make check's runner: started by check.service, runs every program in
 * /tests/bin in order, then powers off. qemu.py reads the console. */
#include <dirent.h>

#include "t.h"

static int
cmp(const void *a, const void *b)
{
	return strcmp(*(char *const *)a, *(char *const *)b);
}

int
main(void)
{
	char *name[64], path[300];
	struct dirent *e;
	int n = 0, i, bad = 0, st;
	pid_t pid;
	DIR *d;

	if (!(d = opendir("/tests/bin"))) {
		perror("check: /tests/bin");
		return 1;
	}
	while ((e = readdir(d)) && n < 64)
		if (e->d_name[0] != '.')
			name[n++] = strdup(e->d_name);
	closedir(d);
	qsort(name, n, sizeof(*name), cmp);
	for (i = 0; i < n; i++) {
		printf("### %s\n", name[i]);
		fflush(stdout);
		snprintf(path, sizeof(path), "/tests/bin/%s", name[i]);
		if ((pid = fork()) == 0) {
			chdir("/tmp");
			execl(path, name[i], (char *)0);
			perror(path);
			_exit(127);
		}
		waitpid(pid, &st, 0);
		if (!WIFEXITED(st) || WEXITSTATUS(st)) {
			printf("### %s failed (%s %d)\n", name[i],
			       WIFEXITED(st) ? "status" : "signal",
			       WIFEXITED(st) ? WEXITSTATUS(st) : WTERMSIG(st));
			bad++;
		}
	}
	printf("check: %d of %d test programs failed\ncheck: done\n", bad, n);
	fflush(stdout);
	execl("/bin/poweroff", "poweroff", (char *)0);
	perror("check: poweroff");
	return 1;
}
