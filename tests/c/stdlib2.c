/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include "check.h"
#include <string.h>
#include <unistd.h>

static int icmp(const void *a, const void *b) {
	return *(const int *)a - *(const int *)b;
}

int main(void) {
	int v[] = {1, 3, 5, 7, 9, 11}, k;
	char *end;
	div_t d;
	ldiv_t ld;
	char *argv1[] = {"prog", "-a", "-bvalue", "-c", "carg", "-ad", "--", "-x", NULL};
	int c, seen = 0;
	CHECK(labs(-5L) == 5 && labs(7L) == 7);
	d = div(17, 5);
	CHECK(d.quot == 3 && d.rem == 2);
	d = div(-17, 5);
	CHECK(d.quot == -3 && d.rem == -2);
	ld = ldiv(100L, 7L);
	CHECK(ld.quot == 14 && ld.rem == 2);
	k = 7;
	CHECK(bsearch(&k, v, 6, sizeof(int), icmp) == &v[3]);
	k = 1;
	CHECK(bsearch(&k, v, 6, sizeof(int), icmp) == &v[0]);
	k = 11;
	CHECK(bsearch(&k, v, 6, sizeof(int), icmp) == &v[5]);
	k = 4;
	CHECK(bsearch(&k, v, 6, sizeof(int), icmp) == NULL);
	CHECK(bsearch(&k, v, 0, sizeof(int), icmp) == NULL);
	CHECK(strtol("777", NULL, 8) == 511 && strtol("z", NULL, 36) == 35 && strtol("101", NULL, 2) == 5);
	CHECK(strtol("0x1f", NULL, 0) == 31 && strtol("017", NULL, 0) == 15 && strtol("19", NULL, 0) == 19);
	CHECK(strtol("  -42xyz", &end, 10) == -42 && strcmp(end, "xyz") == 0);
	CHECK(strtol("Zz", NULL, 36) == 35 * 36 + 35);
	opterr = 0;
	while ((c = getopt(8, argv1, "ab:c:d")) != -1) {
		if (c == 'a') seen |= 1;
		else if (c == 'b' && !strcmp(optarg, "value")) seen |= 2;
		else if (c == 'c' && !strcmp(optarg, "carg")) seen |= 4;
		else if (c == 'd') seen |= 8;
		else seen |= 64;
	}
	CHECK(seen == 15 && optind == 7 && !strcmp(argv1[optind], "-x"));
	DONE();
}
