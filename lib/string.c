/*
 * Copyright (C) 2025 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <string.h>
#include <stdlib.h>

void *memmove(void *d, const void *s, size_t n) {
	char *dp = d;
	const char *sp = s;
	if (dp == sp || n == 0) return d;
	if (dp < sp || dp >= sp + n) return memcpy(d, s, n);
	dp += n;
	sp += n;
	while (n--) *--dp = *--sp;
	return d;
}

int memcmp(const void *a, const void *b, size_t n) {
	const unsigned char *p = a, *q = b;
	for (; n; n--, p++, q++)
		if (*p != *q) return *p - *q;
	return 0;
}

size_t strlen(const char *s) {
	const char *p = s;
	while (*p) p++;
	return p - s;
}

char *strcpy(char *d, const char *s) {
	char *r = d;
	while ((*d++ = *s++) != 0) ;
	return r;
}

char *strncpy(char *d, const char *s, size_t n) {
	size_t i = 0;
	for (; i < n && s[i]; i++) d[i] = s[i];
	for (; i < n; i++) d[i] = 0;
	return d;
}

int strcmp(const char *a, const char *b) {
	while (*a && *a == *b) {
		a++;
		b++;
	}
	return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n) {
	for (; n; n--, a++, b++) {
		if (*a != *b) return (unsigned char)*a - (unsigned char)*b;
		if (!*a) return 0;
	}
	return 0;
}

char *strchr(const char *s, int c) {
	for (;; s++) {
		if (*s == (char)c) return (char *)s;
		if (!*s) return NULL;
	}
}

char *strrchr(const char *s, int c) {
	const char *r = NULL;
	for (;; s++) {
		if (*s == (char)c) r = s;
		if (!*s) return (char *)r;
	}
}

char *strcat(char *d, const char *s) {
	strcpy(d + strlen(d), s);
	return d;
}

char *strncat(char *d, const char *s, size_t n) {
	char *p = d + strlen(d);
	while (n-- && *s) *p++ = *s++;
	*p = 0;
	return d;
}

char *strdup(const char *s) {
	size_t n = strlen(s) + 1;
	char *d = malloc(n);
	if (d) memcpy(d, s, n);
	return d;
}

char *strstr(const char *h, const char *n) {
	size_t l = strlen(n);
	if (!l) return (char *)h;
	for (; *h; h++)
		if (*h == *n && strncmp(h, n, l) == 0) return (char *)h;
	return NULL;
}
