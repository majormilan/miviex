/* Unix domain sockets: pairs, named stream connections, datagrams,
 * errors, nonblocking calls, poll, shutdown, credentials */
#include "t.h"

#include <fcntl.h>
#include <poll.h>
#include <pwd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/un.h>

static struct sockaddr_un
addr(const char *path)
{
	struct sockaddr_un a = { .sun_family = AF_UNIX };

	strcpy(a.sun_path, path);
	return a;
}

static int
listener(const char *path)
{
	struct sockaddr_un a = addr(path);
	int s = socket(AF_UNIX, SOCK_STREAM, 0);

	unlink(path);
	if (bind(s, (struct sockaddr *)&a, sizeof(a)) < 0 || listen(s, 4) < 0)
		return -1;
	return s;
}

static int
dial(const char *path, int type)
{
	struct sockaddr_un a = addr(path);
	int s = socket(AF_UNIX, type, 0);

	return connect(s, (struct sockaddr *)&a, sizeof(a)) < 0 ? -errno : s;
}

static void
pair(void)
{
	char buf[16];
	int sv[2], n;

	check(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0, "socketpair");
	write(sv[0], "hello", 5);
	n = read(sv[1], buf, sizeof(buf));
	check(n == 5 && !memcmp(buf, "hello", 5), "a pair carries bytes (%d)", n);
	close(sv[0]);
	check(read(sv[1], buf, sizeof(buf)) == 0, "read after the other end closed is 0");
	n = send(sv[1], "x", 1, MSG_NOSIGNAL);
	check(n < 0 && errno == EPIPE, "write to a closed end is EPIPE");
	close(sv[1]);
}

/* 100 KiB through a pair: the writer blocks on a full buffer many times */
static void
bulk(void)
{
	static char buf[4096];
	unsigned long sum = 0, want = 0;
	int sv[2], i, n, total = 0;

	socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
	if (fork() == 0) {
		close(sv[0]);
		for (i = 0; i < (int)sizeof(buf); i++)
			buf[i] = (char)i;
		for (i = 0; i < 25; i++)
			write(sv[1], buf, sizeof(buf));
		_exit(0);
	}
	close(sv[1]);
	for (i = 0; i < 25 * (int)sizeof(buf); i++)
		want += (unsigned char)(i % sizeof(buf));
	while ((n = read(sv[0], buf, sizeof(buf))) > 0) {
		for (i = 0; i < n; i++)
			sum += (unsigned char)buf[i];
		total += n;
	}
	wait(NULL);
	close(sv[0]);
	check(total == 25 * (int)sizeof(buf) && sum == want,
	      "100 KiB through a stream pair arrive intact (%d bytes)", total);
}

static void
named(void)
{
	char buf[16];
	struct ucred cr;
	socklen_t len = sizeof(cr);
	struct stat st;
	struct pollfd pfd;
	pid_t child;
	int l, c, n, fd;

	l = listener("/tmp/s1");
	check(l >= 0, "bind and listen on /tmp/s1");
	check(stat("/tmp/s1", &st) == 0 && S_ISSOCK(st.st_mode), "bind makes a socket file");
	fd = open("/tmp/s1", O_RDWR);
	check(fd < 0 && errno == ENXIO, "a socket file cannot be opened (%s)", strerror(errno));

	if ((child = fork()) == 0) {
		int s = dial("/tmp/s1", SOCK_STREAM);
		struct ucred pc;
		socklen_t pl = sizeof(pc);

		if (s < 0)
			_exit(1);
		getsockopt(s, SOL_SOCKET, SO_PEERCRED, &pc, &pl);
		write(s, "ping", 4);
		if (read(s, buf, sizeof(buf)) != 4 || memcmp(buf, "pong", 4))
			_exit(2);
		_exit(pc.pid == (pid_t)getppid() ? 0 : 3);
	}
	pfd.fd = l;
	pfd.events = POLLIN;
	check(poll(&pfd, 1, 2000) == 1 && (pfd.revents & POLLIN), "poll: a waiting connection is POLLIN");
	c = accept(l, NULL, NULL);
	check(c >= 0, "accept");
	n = read(c, buf, sizeof(buf));
	check(n == 4 && !memcmp(buf, "ping", 4), "the server reads what the client sent");
	check(getsockopt(c, SOL_SOCKET, SO_PEERCRED, &cr, &len) == 0 && cr.pid == child,
	      "SO_PEERCRED names the client (pid %d)", (int)cr.pid);
	write(c, "pong", 4);
	waitpid(child, &n, 0);
	check(WIFEXITED(n) && WEXITSTATUS(n) == 0,
	      "the client gets the answer and sees the server's pid (status %d)", WEXITSTATUS(n));
	close(c);

	fcntl(l, F_SETFL, O_NONBLOCK);
	c = accept(l, NULL, NULL);
	check(c < 0 && errno == EAGAIN, "a nonblocking accept without clients is EAGAIN");
	close(l);
	n = dial("/tmp/s1", SOCK_STREAM);
	check(n == -ECONNREFUSED, "connecting after the listener closed is refused (%d)", n);
	unlink("/tmp/s1");
}

static void
errors(void)
{
	struct sockaddr_un a = addr("/tmp/s2");
	int s, t, n, fd;

	n = dial("/tmp/nothing-here", SOCK_STREAM);
	check(n == -ENOENT, "connect to a missing path is ENOENT (%d)", n);
	fd = open("/tmp/plain", O_WRONLY | O_CREAT, 0644);
	close(fd);
	n = dial("/tmp/plain", SOCK_STREAM);
	check(n == -ECONNREFUSED, "connect to a plain file is refused (%d)", n);
	unlink("/tmp/s2");
	s = socket(AF_UNIX, SOCK_STREAM, 0);
	t = socket(AF_UNIX, SOCK_STREAM, 0);
	bind(s, (struct sockaddr *)&a, sizeof(a));
	n = bind(t, (struct sockaddr *)&a, sizeof(a));
	check(n < 0 && errno == EADDRINUSE, "binding a name in use is EADDRINUSE");
	n = socket(AF_INET, SOCK_STREAM, 0);
	check(n < 0 && errno == EAFNOSUPPORT, "AF_INET is not there yet: EAFNOSUPPORT");
	close(s);
	close(t);
	unlink("/tmp/s2");
}

static void
dgram(void)
{
	struct sockaddr_un a = addr("/tmp/d1"), b = addr("/tmp/d2"), from;
	socklen_t flen = sizeof(from);
	struct iovec iov[2] = { { "ab", 2 }, { "cd", 2 } };
	struct msghdr m = { .msg_iov = iov, .msg_iovlen = 2 };
	char buf[16];
	int r, s, n;

	unlink("/tmp/d1");
	unlink("/tmp/d2");
	r = socket(AF_UNIX, SOCK_DGRAM, 0);
	s = socket(AF_UNIX, SOCK_DGRAM, 0);
	bind(r, (struct sockaddr *)&a, sizeof(a));
	sendto(s, "x", 1, 0, (struct sockaddr *)&a, sizeof(a));
	sendto(s, "yz", 2, 0, (struct sockaddr *)&a, sizeof(a));
	n = recvfrom(r, buf, sizeof(buf), 0, (struct sockaddr *)&from, &flen);
	check(n == 1 && flen == sizeof(sa_family_t), "datagrams keep their boundaries; unnamed sender");
	n = recv(r, buf, sizeof(buf), 0);
	check(n == 2 && !memcmp(buf, "yz", 2), "the second datagram on its own");
	bind(s, (struct sockaddr *)&b, sizeof(b));
	connect(s, (struct sockaddr *)&a, sizeof(a));
	send(s, "hi", 2, 0);
	flen = sizeof(from);
	n = recvfrom(r, buf, sizeof(buf), 0, (struct sockaddr *)&from, &flen);
	check(n == 2 && !strcmp(from.sun_path, "/tmp/d2"), "a connected, named sender (%s)", from.sun_path);
	sendmsg(s, &m, 0);
	n = recv(r, buf, sizeof(buf), 0);
	check(n == 4 && !memcmp(buf, "abcd", 4), "sendmsg gathers iovecs into one datagram (%d)", n);
	n = recv(r, buf, sizeof(buf), MSG_DONTWAIT);
	check(n < 0 && errno == EAGAIN, "MSG_DONTWAIT on an empty socket is EAGAIN");
	close(r);
	n = send(s, "z", 1, 0);
	check(n < 0 && errno == ECONNREFUSED, "sending after the receiver closed is refused");
	close(s);
	unlink("/tmp/d1");
	unlink("/tmp/d2");
}

static void
halfclose(void)
{
	char buf[16];
	int sv[2], n;

	socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
	write(sv[0], "last", 4);
	shutdown(sv[0], SHUT_WR);
	n = read(sv[1], buf, sizeof(buf));
	check(n == 4, "shutdown(SHUT_WR): what was sent still arrives");
	check(read(sv[1], buf, sizeof(buf)) == 0, "then the reader gets 0");
	write(sv[1], "back", 4);
	n = read(sv[0], buf, sizeof(buf));
	check(n == 4, "and the other direction still works");
	close(sv[0]);
	close(sv[1]);
}

int
main(void)
{
	struct passwd *pw;

	signal(SIGPIPE, SIG_IGN);
	pair();
	bulk();
	named();
	errors();
	dgram();
	halfclose();
	/* musl asks nscd over a socket first; without one it must fall back */
	pw = getpwnam("root");
	check(pw && pw->pw_uid == 0, "getpwnam still works now that sockets exist");
	return done();
}
