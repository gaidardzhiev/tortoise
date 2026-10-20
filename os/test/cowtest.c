#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int shared_page[1024];

int main(void) {
	int pid, st, i, fds[2], ok = 0;
	char *heap;
	shared_page[0] = 7;
	pid = fork();
	if (pid == 0) {
		shared_page[0] = 1;
		printf("child sees %d\n", shared_page[0]);
		exit(0);
	}
	wait(&st);
	shared_page[0] += 1;
	printf("parent sees %d\n", shared_page[0]);
	heap = malloc(4 << 20);
	if (!heap) {
		printf("malloc failed\n");
		return 1;
	}
	for (i = 0; i < (4 << 20); i += 4096) heap[i] = (char)(i >> 12);
	if (pipe(fds) < 0) return 1;
	for (i = 0; i < 40; i++) {
		pid = fork();
		if (pid < 0) {
			printf("fork %d failed\n", i);
			break;
		}
		if (pid == 0) {
			char c;
			int k, bad = 0;
			close(fds[1]);
			read(fds[0], &c, 1);
			for (k = 0; k < (4 << 20); k += 4096 * 64)
				if (heap[k] != (char)(k >> 12)) bad = 1;
			heap[i * 4096] = 99;
			exit(bad);
		}
	}
	close(fds[0]);
	close(fds[1]);
	for (i = 0; i < 40; i++)
		if (wait(&st) > 0 && st == 0) ok++;
	printf("%d children shared a 4MB heap\n", ok);
	printf("heap[5] in parent is %d\n", heap[5 * 4096]);
	return 0;
}
