/* File system: removed directories, readdir while removing, umount2 */
#include <dirent.h>
#include <fcntl.h>
#include <sys/mount.h>
#include <sys/stat.h>

#include "t.h"

#define NFILES 300

static int
create_in_removed(void)
{
	int fd;

	mkdir("/tmp/gone", 0755);
	if (chdir("/tmp/gone") < 0 || rmdir("/tmp/gone") < 0)
		return 2;
	fd = open("f", O_WRONLY | O_CREAT, 0644);
	return fd < 0 && errno == ENOENT ? 0 : 1;
}

static int
mkdir_in_removed(void)
{
	mkdir("/tmp/gone2", 0755);
	if (chdir("/tmp/gone2") < 0 || rmdir("/tmp/gone2") < 0)
		return 2;
	return mkdir("d", 0755) < 0 && errno == ENOENT ? 0 : 1;
}

int
main(void)
{
	char name[300];
	struct dirent *e;
	DIR *d;
	int i, fd, left, r;

	r = within(2000, create_in_removed);
	check(r == 0, "creating a file in a removed directory fails with ENOENT (got %d)", r);
	r = within(2000, mkdir_in_removed);
	check(r == 0, "mkdir in a removed directory fails with ENOENT (got %d)", r);

	/* what rm -r does: read a bit, remove what was read, read on */
	mkdir("/tmp/many", 0755);
	for (i = 0; i < NFILES; i++) {
		snprintf(name, sizeof(name), "/tmp/many/f%d", i);
		if ((fd = open(name, O_WRONLY | O_CREAT, 0644)) >= 0)
			close(fd);
	}
	if ((d = opendir("/tmp/many"))) {
		while ((e = readdir(d))) {
			if (e->d_name[0] == '.')
				continue;
			snprintf(name, sizeof(name), "/tmp/many/%s", e->d_name);
			unlink(name);
		}
		closedir(d);
	}
	left = 0;
	if ((d = opendir("/tmp/many"))) {
		while ((e = readdir(d)))
			left += e->d_name[0] != '.';
		closedir(d);
	}
	check(left == 0, "removing while reading a directory of %d files leaves none (%d left)", NFILES, left);
	check(rmdir("/tmp/many") == 0, "and the directory can be removed");

	mkdir("/tmp/m", 0755);
	if (mount("tmpfs", "/tmp/m", "tmpfs", 0, NULL) == 0) {
		r = umount2("/tmp/m", MNT_DETACH);
		check(r == 0, "umount2 with MNT_DETACH (%s)", r ? strerror(errno) : "ok");
		if (r)
			umount("/tmp/m");
	} else {
		check(0, "mount tmpfs on /tmp/m: %s", strerror(errno));
	}
	return done();
}
