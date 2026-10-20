#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>

int main(void) {
	int *sh, *pr, st, pid;
	sh = mmap(NULL, 8192, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	pr = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (sh == MAP_FAILED || pr == MAP_FAILED) {
		printf("mmap failed\n");
		return 1;
	}
	printf("shared at %p private at %p\n", (void *)sh, (void *)pr);
	sh[0] = 1;
	pr[0] = 1;
	pid = fork();
	if (pid == 0) {
		sh[0] = 42;
		sh[1024] = 43;
		pr[0] = 42;
		exit(0);
	}
	wait(&st);
	printf("shared after child write: %d %d\n", sh[0], sh[1024]);
	printf("private after child write: %d\n", pr[0]);
	if (mmap((void *)0x50000000, 4096, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) != (void *)0x50000000) printf("fixed mapping failed\n");
	else printf("fixed mapping at 0x50000000 reads %d\n", *(int *)0x50000000);
	printf("bad mmap: %s\n", mmap(NULL, 4096, PROT_READ, MAP_PRIVATE, 3, 0) == MAP_FAILED ? "refused" : "accepted");
	pid = fork();
	if (pid == 0) {
		*(int *)0x50000000 = 5;
		exit(0);
	}
	wait(&st);
	printf("write to read only mapping: status %d\n", st);
	if (munmap(pr, 4096) < 0) printf("munmap failed\n");
	pid = fork();
	if (pid == 0) {
		printf("reading unmapped page\n");
		fflush(stdout);
		st = pr[0];
		exit(0);
	}
	wait(&st);
	printf("access after munmap: status %d\n", st);
	return 0;
}
