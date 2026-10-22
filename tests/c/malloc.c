/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include "check.h"
#include <string.h>

int main(void) {
	char *p[100];
	int i, *a, *b;
	char *q, *r;
	for (i = 0; i < 100; i++) {
		p[i] = malloc(i * 10 + 1);
		CHECK(p[i] != NULL);
		memset(p[i], i, i * 10 + 1);
	}
	for (i = 0; i < 100; i++) CHECK(p[i][i * 10] == (char)i);
	for (i = 0; i < 100; i += 2) free(p[i]);
	q = malloc(50);
	CHECK(q != NULL);
	for (i = 1; i < 100; i += 2) CHECK(p[i][0] == (char)i && p[i][i * 10] == (char)i);
	for (i = 1; i < 100; i += 2) free(p[i]);
	free(q);
	q = malloc(1000);
	r = malloc(1000);
	free(q);
	CHECK(malloc(500) == q);
	a = calloc(10, sizeof(int));
	for (i = 0; i < 10; i++) CHECK(a[i] == 0);
	for (i = 0; i < 10; i++) a[i] = i * i;
	b = realloc(a, 100 * sizeof(int));
	CHECK(b != NULL);
	for (i = 0; i < 10; i++) CHECK(b[i] == i * i);
	b = realloc(b, 4);
	CHECK(b[1] == 1);
	free(b);
	free(r);
	CHECK(realloc(NULL, 10) != NULL);
	free(NULL);
	{
		int total = 0;
		for (i = 0; i < 2000; i++) {
			char *x = malloc(256);
			CHECK(x != NULL);
			x[255] = 1;
			total += x[255];
			free(x);
		}
		CHECK(total == 2000);
	}
	DONE();
}
