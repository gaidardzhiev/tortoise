/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include "check.h"

static int classify(int x) {
	int r = 0;
	switch (x) {
	case 1:
		r += 1;
	case 2:
		r += 2;
		break;
	case 3:
		r = 30;
		break;
	case -1:
		return -100;
	default:
		r = 99;
	case 10:
		r += 10;
	}
	return r;
}

int main(void) {
	int i, j, sum = 0, n;
	for (i = 0; i < 10; i++) sum += i;
	CHECK(sum == 45);
	sum = 0;
	i = 0;
	while (i < 5) {
		i++;
		if (i == 2) continue;
		sum += i;
	}
	CHECK(sum == 13);
	sum = 0;
	i = 0;
	do {
		sum += i;
		i++;
	} while (i < 4);
	CHECK(sum == 6);
	i = 100;
	do i++; while (0);
	CHECK(i == 101);
	sum = 0;
	for (i = 0;; i++) {
		if (i > 100) break;
		if (i % 2) continue;
		sum += i;
	}
	CHECK(sum == 2550);
	sum = 0;
	for (i = 0; i < 5; i++)
		for (j = 0; j < 5; j++) {
			if (j > i) break;
			sum++;
		}
	CHECK(sum == 15);
	CHECK(classify(1) == 3);
	CHECK(classify(2) == 2);
	CHECK(classify(3) == 30);
	CHECK(classify(-1) == -100);
	CHECK(classify(10) == 10);
	CHECK(classify(7) == 109);
	n = 0;
	i = 0;
again:
	n++;
	i++;
	if (i < 3) goto again;
	CHECK(n == 3);
	n = 0;
	for (i = 0; i < 3; i++) {
		for (j = 0; j < 3; j++) {
			if (i == 1 && j == 1) goto out;
			n++;
		}
	}
out:
	CHECK(n == 4);
	if (0) CHECK(0);
	else if (n == 4) n = 5;
	else CHECK(0);
	CHECK(n == 5);
	switch (n) {
	}
	switch (n) {
	default:
		n = 6;
	}
	CHECK(n == 6);
	for (int k = 0; k < 3; k++) n++;
	CHECK(n == 9);
	{
		int n = 1;
		CHECK(n == 1);
	}
	CHECK(n == 9);
	i = 0;
	while (1) {
		switch (i) {
		case 0: i = 5; continue;
		case 5: i = 7; break;
		}
		if (i == 7) break;
	}
	CHECK(i == 7);
	DONE();
}
