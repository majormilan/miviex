/* Processes and signals: reaping, wait4 and process groups, pause,
 * sigsuspend, ppoll's mask, setuid(-1), the idle loop */
#include <poll.h>

#include "t.h"

static volatile sig_atomic_t got;

static void
onusr1(int sig)
{
	(void)sig;
	got = 1;
}

/* a grandchild orphaned to init: init must be told, and reap it */
static int
orphan_reaped(void)
{
	char path[64];
	pid_t g;
	int p[2];

	if (pipe(p) < 0)
		return 2;
	if (fork() == 0) {
		if ((g = fork()) == 0) {
			sleepms(100);   /* outlive the parent, then die */
			_exit(0);
		}
		write(p[1], &g, sizeof(g));
		_exit(0);
	}
	wait(NULL);
	if (read(p[0], &g, sizeof(g)) != sizeof(g))
		return 2;
	snprintf(path, sizeof(path), "/proc/%d/stat", (int)g);
	sleepms(400);
	return access(path, F_OK) == 0;     /* still a zombie: 1 */
}

static int
ignored_sigchld_reaps(void)
{
	signal(SIGCHLD, SIG_IGN);
	if (fork() == 0)
		_exit(0);
	sleepms(100);
	return !(waitpid(-1, NULL, WNOHANG) < 0 && errno == ECHILD);
}

/* waitpid(0) waits only for children in our process group: the other
 * child dies first, in a group of its own, and must not be returned */
static int
wait_own_group(void)
{
	pid_t mine, other;

	if ((mine = fork()) == 0) {
		sleepms(200);
		_exit(0);
	}
	if ((other = fork()) == 0) {
		setpgid(0, 0);
		sleepms(20);
		_exit(0);
	}
	setpgid(other, other);
	sleepms(100);                       /* other is a zombie by now */
	return waitpid(0, NULL, 0) != mine;
}

static int
pause_returns(void)
{
	signal(SIGUSR1, onusr1);
	if (fork() == 0) {
		sleepms(100);
		kill(getppid(), SIGUSR1);
		_exit(0);
	}
	return !(pause() < 0 && errno == EINTR && got);
}

static int
sigsuspend_unblocks(void)
{
	sigset_t block, none;

	signal(SIGUSR1, onusr1);
	sigemptyset(&block);
	sigaddset(&block, SIGUSR1);
	sigprocmask(SIG_BLOCK, &block, NULL);
	if (fork() == 0) {
		sleepms(100);
		kill(getppid(), SIGUSR1);
		_exit(0);
	}
	sleepms(200);                       /* pending, blocked */
	sigemptyset(&none);
	return !(sigsuspend(&none) < 0 && errno == EINTR && got);
}

/* a blocked signal that ppoll's mask lets through ends the wait */
static int
ppoll_mask(void)
{
	struct timespec ts = { 2, 0 };
	sigset_t block, none;
	long long t;
	int r;

	signal(SIGUSR1, onusr1);
	sigemptyset(&block);
	sigaddset(&block, SIGUSR1);
	sigprocmask(SIG_BLOCK, &block, NULL);
	if (fork() == 0) {
		sleepms(100);
		kill(getppid(), SIGUSR1);
		_exit(0);
	}
	sigemptyset(&none);
	t = ms();
	r = ppoll(NULL, 0, &ts, &none);
	return !(r < 0 && errno == EINTR && got && ms() - t < 1000);
}

static int
setuid_minus_one(void)
{
	return !(setuid((uid_t)-1) < 0 && errno == EINVAL && getuid() == 0);
}

int
main(void)
{
	char buf[512], *p;
	long long t;
	FILE *fp;
	int i, r;

	r = within(3000, orphan_reaped);
	check(r == 0, "an orphan that dies is reaped by init at once (got %d)", r);
	r = within(3000, ignored_sigchld_reaps);
	check(r == 0, "children are not left as zombies when SIGCHLD is ignored (got %d)", r);
	r = within(3000, wait_own_group);
	check(r == 0, "waitpid(0) waits for the own process group only (got %d)", r);
	r = within(3000, pause_returns);
	check(r == 0, "pause returns EINTR after a signal (got %d)", r);
	r = within(3000, sigsuspend_unblocks);
	check(r == 0, "sigsuspend takes a pending blocked signal (got %d)", r);
	r = within(5000, ppoll_mask);
	check(r == 0, "ppoll applies its signal mask (got %d)", r);
	r = within(3000, setuid_minus_one);
	check(r == 0, "setuid(-1) is EINVAL (got %d)", r);

	/* sleeping 1 ms twenty times on an idle system */
	t = ms();
	for (i = 0; i < 20; i++)
		sleepms(1);
	t = ms() - t;
	/* 100 Hz ticks: 10 ms each at best, not the idle quantum (170 ms) */
	check(t < 500, "20 sleeps of 1 ms on an idle system take %lld ms", t);

	/* field 7 of /proc/1/stat: init's controlling terminal */
	r = -1;
	if ((fp = fopen("/proc/1/stat", "r"))) {
		if (fgets(buf, sizeof(buf), fp) && (p = strrchr(buf, ')')))
			sscanf(p + 2, "%*c %*d %*d %*d %d", &r);
		fclose(fp);
	}
	check(r == 0, "init has no controlling terminal (tty_nr %d)", r);
	return done();
}
