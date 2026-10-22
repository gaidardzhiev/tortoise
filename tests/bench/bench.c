/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static char comp[20000];

static int fib(int n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }

static int cmp(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }

int main(void) {
	int i, j, round, count = 0, a[2000];
	char buf[200];
	for (round = 0; round < 20; round++) {
		memset(comp, 0, sizeof(comp));
		for (i = 2; i * i < 20000; i++)
			if (!comp[i])
				for (j = i * i; j < 20000; j += i) comp[j] = 1;
		for (i = 2; i < 20000; i++) count += !comp[i];
	}
	count += fib(24);
	for (round = 0; round < 5; round++) {
		for (i = 0; i < 2000; i++) a[i] = (i * 7919) % 2003;
		qsort(a, 2000, sizeof(int), cmp);
	}
	for (i = 0; i < 2000; i++) {
		sprintf(buf, "%d:%s:%x", i, "bench", i * 3);
		count += (int)strlen(buf);
	}
	printf("%d\n", count);
	return 0;
}
