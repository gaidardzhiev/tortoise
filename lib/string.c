/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

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

void *memchr(const void *s, int c, size_t n) {
	const unsigned char *p = s;
	size_t i;
	for (i = 0; i < n; i++)
		if (p[i] == (unsigned char)c) return (void *)(p + i);
	return NULL;
}

size_t strnlen(const char *s, size_t n) {
	size_t i = 0;
	while (i < n && s[i]) i++;
	return i;
}

char *strndup(const char *s, size_t n) {
	size_t l = strnlen(s, n);
	char *d = malloc(l + 1);
	if (!d) return NULL;
	memcpy(d, s, l);
	d[l] = 0;
	return d;
}

size_t strspn(const char *s, const char *set) {
	size_t i = 0;
	while (s[i] && strchr(set, s[i])) i++;
	return i;
}

size_t strcspn(const char *s, const char *set) {
	size_t i = 0;
	while (s[i] && !strchr(set, s[i])) i++;
	return i;
}

char *strpbrk(const char *s, const char *set) {
	s += strcspn(s, set);
	return *s ? (char *)s : NULL;
}

char *strtok_r(char *s, const char *delim, char **save) {
	char *t;
	if (!s) s = *save;
	if (!s) return NULL;
	s += strspn(s, delim);
	if (!*s) {
		*save = NULL;
		return NULL;
	}
	t = s + strcspn(s, delim);
	if (*t) {
		*t = 0;
		*save = t + 1;
	} else {
		*save = NULL;
	}
	return s;
}

char *strtok(char *s, const char *delim) {
	static char *save;
	return strtok_r(s, delim, &save);
}

static int lower(int c) {
	return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

int strcasecmp(const char *a, const char *b) {
	while (*a && lower((unsigned char)*a) == lower((unsigned char)*b)) {
		a++;
		b++;
	}
	return lower((unsigned char)*a) - lower((unsigned char)*b);
}

int strncasecmp(const char *a, const char *b, size_t n) {
	if (n == 0) return 0;
	while (--n && *a && lower((unsigned char)*a) == lower((unsigned char)*b)) {
		a++;
		b++;
	}
	return lower((unsigned char)*a) - lower((unsigned char)*b);
}

