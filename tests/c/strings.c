/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include "check.h"
#include <string.h>
#include <ctype.h>

int main(void) {
	char a[32], b[32], *d;
	int i;
	CHECK(strlen("") == 0 && strlen("hello") == 5);
	CHECK(strcmp(strcpy(a, "abc"), "abc") == 0);
	CHECK(strcmp("abc", "abd") < 0 && strcmp("abd", "abc") > 0 && strcmp("ab", "abc") < 0);
	CHECK(strcmp("\xff", "a") > 0);
	CHECK(strncmp("abcdef", "abcxyz", 3) == 0 && strncmp("abcdef", "abcxyz", 4) < 0 && strncmp("a", "b", 0) == 0);
	memset(b, 'z', sizeof(b));
	strncpy(b, "hi", 5);
	CHECK(b[0] == 'h' && b[2] == 0 && b[4] == 0 && b[5] == 'z');
	strncpy(b, "abcdef", 3);
	CHECK(b[0] == 'a' && b[2] == 'c' && b[3] == 0);
	strcpy(a, "foo");
	strcat(a, "bar");
	CHECK(strcmp(a, "foobar") == 0);
	strncat(a, "bazqux", 3);
	CHECK(strcmp(a, "foobarbaz") == 0);
	CHECK(strchr(a, 'b') == a + 3 && strchr(a, 'q') == NULL && strchr(a, 0) == a + 9);
	CHECK(strrchr(a, 'b') == a + 6 && strrchr(a, 'q') == NULL);
	CHECK(strstr(a, "bar") == a + 3 && strstr(a, "xyz") == NULL && strstr(a, "") == a);
	d = strdup(a);
	CHECK(d && d != a && strcmp(d, a) == 0);
	free(d);
	for (i = 0; i < 32; i++) b[i] = (char)i;
	memcpy(a, b, 32);
	CHECK(memcmp(a, b, 32) == 0);
	a[31] = 100;
	CHECK(memcmp(a, b, 32) > 0 && memcmp(b, a, 32) < 0);
	memmove(b + 1, b, 10);
	CHECK(b[0] == 0 && b[1] == 0 && b[2] == 1 && b[10] == 9 && b[11] == 11);
	memmove(b, b + 1, 10);
	CHECK(b[0] == 0 && b[1] == 1 && b[9] == 9);
	memset(a, 0, 32);
	CHECK(a[0] == 0 && a[31] == 0);
	memcpy(a + 1, "xyz", 3);
	CHECK(a[1] == 'x' && a[3] == 'z');
	CHECK(isdigit('5') && !isdigit('a') && isalpha('Q') && isalnum('9') && isspace('\t') && !isspace('x'));
	CHECK(isupper('A') && islower('a') && toupper('a') == 'A' && tolower('Z') == 'z' && toupper('1') == '1');
	CHECK(atoi("  -123x") == -123 && strtol("0x1F", NULL, 0) == 31 && strtoul("777", NULL, 8) == 511);
	{
		char *e;
		CHECK(strtol("42rest", &e, 10) == 42 && strcmp(e, "rest") == 0);
	}
	CHECK(abs(-5) == 5 && abs(5) == 5);
	DONE();
}
