/* exec: nothing may be mapped at page 0 */
#include <fcntl.h>
#include <stdint.h>

#include "t.h"

/* A static ELF whose code is exit(42), loaded at vaddr */
static int
mkelf(const char *path, uint64_t vaddr)
{
	static const unsigned char code[] = {
		0xb8, 0x3c, 0, 0, 0,            /* mov eax, 60 */
		0xbf, 0x2a, 0, 0, 0,            /* mov edi, 42 */
		0x0f, 0x05,                     /* syscall */
	};
	unsigned char f[0x78 + sizeof(code)] = { 0 };
	uint64_t entry = vaddr + 0x78, size = sizeof(f);
	int fd;

	memcpy(f, "\177ELF\2\1\1", 7);
	f[16] = 2;                              /* ET_EXEC */
	f[18] = 0x3e;                           /* x86-64 */
	f[20] = 1;
	memcpy(f + 24, &entry, 8);
	f[32] = 64;                             /* e_phoff */
	f[52] = 64;                             /* e_ehsize */
	f[54] = 56;                             /* e_phentsize */
	f[56] = 1;                              /* e_phnum */
	f[64] = 1;                              /* PT_LOAD */
	f[68] = 5;                              /* R+X */
	memcpy(f + 80, &vaddr, 8);
	memcpy(f + 88, &vaddr, 8);
	memcpy(f + 96, &size, 8);
	memcpy(f + 104, &size, 8);
	f[113] = 0x10;                          /* p_align 0x1000 */
	memcpy(f + 0x78, code, sizeof(code));
	if ((fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0755)) < 0)
		return -1;
	write(fd, f, sizeof(f));
	close(fd);
	return 0;
}

static int
run(const char *path)
{
	pid_t pid;
	int st;

	if ((pid = fork()) == 0) {
		execl(path, path, (char *)0);
		_exit(3);
	}
	waitpid(pid, &st, 0);
	return WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);
}

int
main(void)
{
	int r;

	mkelf("/tmp/elf-normal", 0x400000);
	mkelf("/tmp/elf-page0", 0);
	r = run("/tmp/elf-normal");
	check(r == 42, "a hand-made ELF at 0x400000 runs (status %d)", r);
	r = run("/tmp/elf-page0");
	check(r != 42, "an ELF loaded at page 0 is refused (status %d)", r);
	return done();
}
