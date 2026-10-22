/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include "check.h"
#include <string.h>

int main(void) {
	int a, b, n;
	unsigned u, x, o;
	char s[32], t[32], c, cc[4];
	CHECK(sscanf("12 -34", "%d %d", &a, &b) == 2 && a == 12 && b == -34);
	CHECK(sscanf("4000000000 ff 777", "%u %x %o", &u, &x, &o) == 3 && u == 4000000000u && x == 255 && o == 511);
	CHECK(sscanf("0x1A", "%x", &x) == 1 && x == 26);
	CHECK(sscanf("0x10 010 10", "%i %i %i", &a, &b, &n) == 3 && a == 16 && b == 8 && n == 10);
	CHECK(sscanf("hello world", "%s %s", s, t) == 2 && !strcmp(s, "hello") && !strcmp(t, "world"));
	CHECK(sscanf("abcdef", "%3s%s", s, t) == 2 && !strcmp(s, "abc") && !strcmp(t, "def"));
	CHECK(sscanf("12345", "%2d%3d", &a, &b) == 2 && a == 12 && b == 345);
	CHECK(sscanf(" x", "%c", &c) == 1 && c == ' ');
	CHECK(sscanf(" x", " %c", &c) == 1 && c == 'x');
	CHECK(sscanf("abcd", "%3c", cc) == 1 && cc[0] == 'a' && cc[2] == 'c');
	CHECK(sscanf("key=value;", "%[^=]=%[a-z]", s, t) == 2 && !strcmp(s, "key") && !strcmp(t, "value"));
	CHECK(sscanf("aaab", "%[a]", s) == 1 && !strcmp(s, "aaa"));
	CHECK(sscanf("]x", "%[]]", s) == 1 && !strcmp(s, "]"));
	CHECK(sscanf("10:20", "%d:%d", &a, &b) == 2 && a == 10 && b == 20);
	CHECK(sscanf("10-20", "%d:%d", &a, &b) == 1 && a == 10);
	CHECK(sscanf("skip 5", "%*s %d", &a) == 1 && a == 5);
	CHECK(sscanf("abc 7", "%s%n", s, &n) == 1 && n == 3);
	CHECK(sscanf("100%", "%d%%", &a) == 1 && a == 100);
	CHECK(sscanf("", "%d", &a) == -1);
	CHECK(sscanf("   ", "%s", s) == -1);
	CHECK(sscanf("x", "%d", &a) == 0);
	DONE();
}
