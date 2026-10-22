/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>

typedef struct Block {
	unsigned size;
	struct Block *next;
} Block;

static Block *free_list;
static unsigned rand_state = 1;

void *malloc(size_t n) {
	Block *b, **pp;
	unsigned need;
	if (n == 0) n = 1;
	if (n > 0x7FFFFFF0u) return NULL;
	need = (n + sizeof(Block) + 7) & ~7u;
	for (pp = &free_list; (b = *pp) != NULL; pp = &b->next) {
		if (b->size >= need) {
			if (b->size - need >= 32) {
				Block *r = (Block *)((char *)b + need);
				r->size = b->size - need;
				r->next = b->next;
				*pp = r;
				b->size = need;
			} else {
				*pp = b->next;
			}
			b->next = NULL;
			return b + 1;
		}
	}
	b = sbrk(need);
	if (b == (Block *)-1 || b == NULL) return NULL;
	b->size = need;
	b->next = NULL;
	return b + 1;
}

void free(void *p) {
	Block *b, *cur, *prev = NULL;
	if (!p) return;
	b = (Block *)p - 1;
	for (cur = free_list; cur && cur < b; cur = cur->next) prev = cur;
	b->next = cur;
	if (prev) prev->next = b;
	else free_list = b;
	if (cur && (char *)b + b->size == (char *)cur) {
		b->size += cur->size;
		b->next = cur->next;
	}
	if (prev && (char *)prev + prev->size == (char *)b) {
		prev->size += b->size;
		prev->next = b->next;
	}
}

void *calloc(size_t n, size_t m) {
	void *p;
	if (m && n > 0x7FFFFFFFu / m) return NULL;
	p = malloc(n * m);
	if (p) memset(p, 0, n * m);
	return p;
}

void *realloc(void *p, size_t n) {
	Block *b;
	void *q;
	unsigned have;
	if (!p) return malloc(n);
	if (n == 0) {
		free(p);
		return NULL;
	}
	b = (Block *)p - 1;
	have = b->size - sizeof(Block);
	if (have >= n) return p;
	q = malloc(n);
	if (!q) return NULL;
	memcpy(q, p, have);
	free(p);
	return q;
}

unsigned long strtoul(const char *s, char **end, int base) {
	unsigned long v = 0;
	int neg = 0, any = 0;
	while (isspace(*s)) s++;
	if (*s == '+' || *s == '-') neg = *s++ == '-';
	if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X') && isxdigit(s[2])) {
		s += 2;
		base = 16;
	}
	if (base == 0) base = s[0] == '0' ? 8 : 10;
	for (;; s++) {
		int d;
		if (isdigit(*s)) d = *s - '0';
		else if (isalpha(*s)) d = tolower(*s) - 'a' + 10;
		else break;
		if (d >= base) break;
		v = v * base + d;
		any = 1;
	}
	if (end) *end = (char *)s;
	(void)any;
	return neg ? -v : v;
}

long strtol(const char *s, char **end, int base) {
	return (long)strtoul(s, end, base);
}

int atoi(const char *s) {
	return (int)strtol(s, NULL, 10);
}

int abs(int x) {
	return x < 0 ? -x : x;
}

static void swap(char *a, char *b, size_t n) {
	while (n--) {
		char t = *a;
		*a++ = *b;
		*b++ = t;
	}
}

static void qs(char *base, size_t n, size_t size, int (*cmp)(const void *, const void *)) {
	while (n > 1) {
		size_t i, last = 0;
		swap(base, base + (n / 2) * size, size);
		for (i = 1; i < n; i++) {
			if (cmp(base + i * size, base) < 0) {
				last++;
				swap(base + last * size, base + i * size, size);
			}
		}
		swap(base, base + last * size, size);
		if (last < n - last - 1) {
			qs(base, last, size, cmp);
			base += (last + 1) * size;
			n -= last + 1;
		} else {
			qs(base + (last + 1) * size, n - last - 1, size, cmp);
			n = last;
		}
	}
}

void qsort(void *base, size_t n, size_t size, int (*cmp)(const void *, const void *)) {
	qs(base, n, size, cmp);
}

int rand(void) {
	rand_state = rand_state * 1103515245 + 12345;
	return (rand_state >> 16) & 0x7FFF;
}

void srand(unsigned seed) {
	rand_state = seed;
}

char **environ;
static char **own_env;

static int env_find(const char *name, unsigned n) {
	int i;
	if (!environ) return -1;
	for (i = 0; environ[i]; i++)
		if (strncmp(environ[i], name, n) == 0 && environ[i][n] == '=') return i;
	return -1;
}

char *getenv(const char *name) {
	int i = env_find(name, strlen(name));
	return i < 0 ? NULL : environ[i] + strlen(name) + 1;
}

static int env_count(void) {
	int n = 0;
	while (environ && environ[n]) n++;
	return n;
}

static int env_own(int extra) {
	int n = env_count(), i;
	char **e = malloc((n + extra + 1) * sizeof(char *));
	if (!e) return -1;
	for (i = 0; i < n; i++) e[i] = environ[i];
	e[n] = NULL;
	if (own_env) free(own_env);
	own_env = environ = e;
	return 0;
}

int putenv(char *s) {
	char *eq = strchr(s, '=');
	int i, n;
	if (!eq) return -1;
	i = env_find(s, eq - s);
	if (i >= 0) {
		if (environ != own_env && env_own(0) < 0) return -1;
		environ[i] = s;
		return 0;
	}
	if (env_own(1) < 0) return -1;
	n = env_count();
	environ[n] = s;
	environ[n + 1] = NULL;
	return 0;
}

int setenv(const char *name, const char *value, int overwrite) {
	unsigned ln = strlen(name), lv = strlen(value);
	char *s;
	if (ln == 0 || strchr(name, '=')) {
		errno = EINVAL;
		return -1;
	}
	if (env_find(name, ln) >= 0 && !overwrite) return 0;
	s = malloc(ln + lv + 2);
	if (!s) return -1;
	memcpy(s, name, ln);
	s[ln] = '=';
	memcpy(s + ln + 1, value, lv + 1);
	return putenv(s);
}

int unsetenv(const char *name) {
	int i = env_find(name, strlen(name));
	if (i < 0) return 0;
	if (environ != own_env && env_own(0) < 0) return -1;
	for (; environ[i]; i++) environ[i] = environ[i + 1];
	return 0;
}

void __stdio_flush_all(void);

static void (*exit_fns[32])(void);
static int nexit_fns;

int atexit(void (*fn)(void)) {
	if (nexit_fns >= 32) return -1;
	exit_fns[nexit_fns++] = fn;
	return 0;
}

void exit(int status) {
	while (nexit_fns > 0) exit_fns[--nexit_fns]();
	__stdio_flush_all();
	_exit(status);
}

void __assert_fail(const char *expr, const char *file, int line) {
	fprintf(stderr, "assertion failed: %s, file %s, line %d\n", expr, file, line);
	exit(134);
}

long labs(long x) {
	return x < 0 ? -x : x;
}

div_t div(int a, int b) {
	div_t r;
	r.quot = a / b;
	r.rem = a % b;
	return r;
}

ldiv_t ldiv(long a, long b) {
	ldiv_t r;
	r.quot = a / b;
	r.rem = a % b;
	return r;
}

void *bsearch(const void *key, const void *base, size_t n, size_t size, int (*cmp)(const void *, const void *)) {
	size_t lo = 0, hi = n;
	while (lo < hi) {
		size_t mid = lo + (hi - lo) / 2;
		const char *p = (const char *)base + mid * size;
		int c = cmp(key, p);
		if (c == 0) return (void *)p;
		if (c < 0) hi = mid;
		else lo = mid + 1;
	}
	return NULL;
}

char *optarg;
int optind = 1, opterr = 1, optopt;
static int optpos = 1;

int getopt(int argc, char *const argv[], const char *opts) {
	const char *o;
	char c;
	if (optind >= argc || !argv[optind] || argv[optind][0] != '-' || !argv[optind][1]) return -1;
	if (!strcmp(argv[optind], "--")) {
		optind++;
		return -1;
	}
	c = argv[optind][optpos++];
	o = strchr(opts, c);
	if (!o || c == ':') {
		optopt = c;
		if (opterr && opts[0] != ':') fprintf(stderr, "%s: unknown option -%c\n", argv[0], c);
		if (!argv[optind][optpos]) {
			optind++;
			optpos = 1;
		}
		return '?';
	}
	if (o[1] == ':') {
		if (argv[optind][optpos]) optarg = argv[optind] + optpos;
		else if (optind + 1 < argc) optarg = argv[++optind];
		else {
			optopt = c;
			optind++;
			optpos = 1;
			if (opterr && opts[0] != ':') fprintf(stderr, "%s: option -%c needs an argument\n", argv[0], c);
			return opts[0] == ':' ? ':' : '?';
		}
		optind++;
		optpos = 1;
		return c;
	}
	if (!argv[optind][optpos]) {
		optind++;
		optpos = 1;
	}
	return c;
}
