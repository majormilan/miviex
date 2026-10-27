/* Pipes: atomic writes and writevs up to PIPE_BUF, zero-length reads */
#include <limits.h>
#include <sys/uio.h>

#include "t.h"

#define WRITERS 4
#define RECORDS 100
#define REC 512

static int p[2];

static int
zero_read(void)
{
	char c;

	return read(p[0], &c, 0) != 0;
}

int
main(void)
{
	char rec[REC], buf[REC];
	struct iovec iov[2];
	int i, j, k, bad = 0, total = 0, n, r;

	/* writers race; every record must arrive whole */
	pipe(p);
	for (i = 0; i < WRITERS; i++) {
		if (fork() == 0) {
			close(p[0]);
			memset(rec, 'a' + i, REC);
			/* half of them in two pieces: writev is one write too */
			iov[0].iov_base = rec;
			iov[0].iov_len = REC / 2;
			iov[1].iov_base = rec + REC / 2;
			iov[1].iov_len = REC / 2;
			for (j = 0; j < RECORDS; j++) {
				if (i % 2)
					writev(p[1], iov, 2);
				else
					write(p[1], rec, REC);
			}
			_exit(0);
		}
	}
	close(p[1]);
	for (;;) {
		for (n = 0; n < REC; n += k)
			if ((k = read(p[0], buf + n, REC - n)) <= 0)
				break;
		if (n < REC)
			break;
		total++;
		for (k = 1; k < REC; k++)
			if (buf[k] != buf[0])
				break;
		bad += k < REC;
	}
	while (wait(NULL) > 0)
		;
	close(p[0]);
	check(total == WRITERS * RECORDS && !bad,
	      "writes and writevs of %d bytes (<= PIPE_BUF %d) do not interleave (%d of %d records mixed)",
	      REC, PIPE_BUF, bad, total);

	pipe(p);
	r = within(1000, zero_read);
	check(r == 0, "a zero-length read on an empty pipe returns at once (got %d)", r);
	return done();
}
