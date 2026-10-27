/* Helpers for the tests make check runs on Miviex. A test prints one
 * "ok - what" or "not ok - what" line per check and exits non-zero if
 * any failed. Static musl programs: no shell needed. */
#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int failed;

static void
check(int cond, const char *fmt, ...)
{
	va_list ap;

	printf(cond ? "ok - " : "not ok - ");
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	putchar('\n');
	fflush(stdout);
	if (!cond)
		failed++;
}

static long long
ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;
}

static void
sleepms(long n)
{
	struct timespec ts = { n / 1000, n % 1000 * 1000000 };

	nanosleep(&ts, NULL);
}

/* Runs fn in a child; its exit status, or -1 if it did not finish within
 * limit milliseconds (it is killed then). There is no alarm() yet. */
static int
within(long limit, int (*fn)(void))
{
	long long end = ms() + limit;
	pid_t pid;
	int st;

	fflush(stdout);
	if ((pid = fork()) == 0)
		_exit(fn());
	while (waitpid(pid, &st, WNOHANG) == 0) {
		if (ms() >= end) {
			kill(pid, SIGKILL);
			waitpid(pid, &st, 0);
			return -1;
		}
		sleepms(5);
	}
	return WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);
}

static int
done(void)
{
	return failed != 0;
}
