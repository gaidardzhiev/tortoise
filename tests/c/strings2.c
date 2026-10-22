/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include "check.h"
#include <string.h>
#include <errno.h>

int main(void) {
	char buf[64], *save, *t, *d;
	const char *s = "hello world";
	CHECK(memchr(s, 'o', 11) == s + 4 && memchr(s, 'z', 11) == NULL && memchr(s, 'w', 5) == NULL);
	CHECK(strnlen("abc", 10) == 3 && strnlen("abcdef", 4) == 4 && strnlen("", 3) == 0);
	d = strndup("abcdef", 3);
	CHECK(d && strcmp(d, "abc") == 0);
	free(d);
	d = strndup("ab", 10);
	CHECK(d && strcmp(d, "ab") == 0);
	free(d);
	CHECK(strspn("aabbcx", "abc") == 5 && strspn("x", "abc") == 0 && strspn("", "a") == 0);
	CHECK(strcspn("abc:def", ":;") == 3 && strcspn("abc", ":") == 3);
	CHECK(strpbrk("key=value", "=:") == strchr("key=value", '=') || 1);
	strcpy(buf, "key=value");
	CHECK(strpbrk(buf, "=:") == buf + 3 && strpbrk(buf, "#") == NULL);
	strcpy(buf, "  one, two,,three ");
	t = strtok(buf, " ,");
	CHECK(t && strcmp(t, "one") == 0);
	t = strtok(NULL, " ,");
	CHECK(t && strcmp(t, "two") == 0);
	t = strtok(NULL, " ,");
	CHECK(t && strcmp(t, "three") == 0);
	CHECK(strtok(NULL, " ,") == NULL);
	strcpy(buf, "a:b:c");
	t = strtok_r(buf, ":", &save);
	CHECK(t && !strcmp(t, "a"));
	t = strtok_r(NULL, ":", &save);
	CHECK(t && !strcmp(t, "b"));
	t = strtok_r(NULL, ":", &save);
	CHECK(t && !strcmp(t, "c") && strtok_r(NULL, ":", &save) == NULL);
	CHECK(strcasecmp("HeLLo", "hello") == 0 && strcasecmp("abc", "ABD") < 0 && strcasecmp("b", "A") > 0);
	CHECK(strncasecmp("HELLOx", "helloy", 5) == 0 && strncasecmp("ab", "AC", 2) < 0 && strncasecmp("a", "b", 0) == 0);
	CHECK(strcmp(strerror(ENOENT), "No such file or directory") == 0);
	CHECK(strcmp(strerror(EACCES), "Permission denied") == 0);
	CHECK(strcmp(strerror(ECONNREFUSED), "Connection refused") == 0);
	CHECK(strcmp(strerror(EADDRINUSE), "Address already in use") == 0);
	CHECK(strcmp(strerror(ELOOP), "Too many levels of symbolic links") == 0);
	CHECK(strncmp(strerror(999), "Unknown error 999", 17) == 0);
	DONE();
}
