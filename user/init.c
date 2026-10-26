#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv) {
	printf("Hello from %s, a static musl binary (argc=%d, pid=%d)\n",
	       argv[0], argc, getpid());
	return 0;
}
