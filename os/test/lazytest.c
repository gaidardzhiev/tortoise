#include <stdio.h>
#include <unistd.h>

static int depth(int n) {
	char buf[1024];
	buf[0] = (char)n;
	buf[1023] = 0;
	if (n == 0) return buf[1023];
	return depth(n - 1) + (buf[0] == (char)n);
}

int main(void) {
	char *p = sbrk(0), *q;
	int i, sum = 0;
	q = sbrk(64 << 20);
	if (q == (char *)-1) {
		printf("sbrk of 64MB failed\n");
		return 1;
	}
	printf("sbrk of 64MB on a 32MB machine succeeded\n");
	for (i = 0; i < 16; i++) {
		sum += q[i * (4 << 20)];
		q[i * (4 << 20)] = 1;
	}
	printf("touched 16 pages, sum of fresh bytes %d\n", sum);
	if (sbrk(-(64 << 20)) != q + (64 << 20)) printf("shrink failed\n");
	printf("break restored %s\n", sbrk(0) == p ? "yes" : "no");
	printf("stack recursion of 600KB: %d frames intact\n", depth(600));
	return 0;
}
