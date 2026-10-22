/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

typedef struct {
	const char *s;
	FILE *fp;
	int count;
} In;

static int inget(In *in) {
	int c;
	if (in->fp) c = fgetc(in->fp);
	else c = *in->s ? (unsigned char)*in->s++ : EOF;
	if (c != EOF) in->count++;
	return c;
}

static void inunget(In *in, int c) {
	if (c == EOF) return;
	in->count--;
	if (in->fp) ungetc(c, in->fp);
	else in->s--;
}

static int doscanf(In *in, const char *f, va_list ap) {
	int assigned = 0, any = 0;
	for (; *f; f++) {
		int c, width = 0, skip = 0, base = 10;
		if (isspace((unsigned char)*f)) {
			while ((c = inget(in)) != EOF && isspace(c)) ;
			inunget(in, c);
			continue;
		}
		if (*f != '%') {
			c = inget(in);
			if (c != (unsigned char)*f) {
				inunget(in, c);
				return c == EOF && !any ? EOF : assigned;
			}
			continue;
		}
		f++;
		if (*f == '*') {
			skip = 1;
			f++;
		}
		while (*f >= '0' && *f <= '9') width = width * 10 + *f++ - '0';
		while (*f == 'l' || *f == 'h') f++;
		if (*f == '%') {
			while ((c = inget(in)) != EOF && isspace(c)) ;
			if (c != '%') {
				inunget(in, c);
				return assigned;
			}
			continue;
		}
		if (*f == 'n') {
			if (!skip) *va_arg(ap, int *) = in->count;
			continue;
		}
		if (*f == 'c') {
			char *d = skip ? NULL : va_arg(ap, char *);
			int i;
			if (!width) width = 1;
			for (i = 0; i < width; i++) {
				c = inget(in);
				if (c == EOF) return any ? assigned : EOF;
				if (d) d[i] = (char)c;
			}
			any = 1;
			if (!skip) assigned++;
			continue;
		}
		if (*f == 's' || *f == '[') {
			char set[256], *d = skip ? NULL : va_arg(ap, char *);
			int i = 0, neg = 0;
			memset(set, 0, sizeof(set));
			if (*f == '[') {
				f++;
				if (*f == '^') {
					neg = 1;
					f++;
				}
				if (*f == ']') set[(unsigned char)*f++] = 1;
				while (*f && *f != ']') {
					if (f[1] == '-' && f[2] && f[2] != ']') {
						int a;
						for (a = (unsigned char)f[0]; a <= (unsigned char)f[2]; a++) set[a] = 1;
						f += 3;
					} else {
						set[(unsigned char)*f++] = 1;
					}
				}
				if (!*f) f--;
			} else {
				while ((c = inget(in)) != EOF && isspace(c)) ;
				inunget(in, c);
				neg = 1;
				set[' '] = set['\t'] = set['\n'] = set['\r'] = set['\v'] = set['\f'] = 1;
			}
			while (!width || i < width) {
				c = inget(in);
				if (c == EOF || (set[c] != 0) == neg) {
					inunget(in, c);
					break;
				}
				if (d) d[i] = (char)c;
				i++;
			}
			if (i == 0) return c == EOF && !any ? EOF : assigned;
			if (d) d[i] = 0;
			any = 1;
			if (!skip) assigned++;
			continue;
		}
		if (*f == 'd' || *f == 'i' || *f == 'u' || *f == 'x' || *f == 'X' || *f == 'o' || *f == 'p') {
			unsigned v = 0;
			int neg = 0, digits = 0, n = 0;
			if (*f == 'x' || *f == 'X' || *f == 'p') base = 16;
			else if (*f == 'o') base = 8;
			else if (*f == 'i') base = 0;
			if (!width) width = 64;
			while ((c = inget(in)) != EOF && isspace(c)) ;
			if (c == '-' || c == '+') {
				neg = c == '-';
				c = inget(in);
				n++;
			}
			if ((base == 0 || base == 16) && c == '0' && n < width) {
				int c2 = inget(in);
				n++;
				digits = 1;
				if ((c2 == 'x' || c2 == 'X') && n < width) {
					base = 16;
					digits = 0;
					c = inget(in);
					n++;
				} else {
					if (base == 0) base = 8;
					c = c2;
				}
			}
			if (base == 0) base = 10;
			while (c != EOF && n < width) {
				int dv;
				if (c >= '0' && c <= '9') dv = c - '0';
				else if (c >= 'a' && c <= 'f') dv = c - 'a' + 10;
				else if (c >= 'A' && c <= 'F') dv = c - 'A' + 10;
				else break;
				if (dv >= base) break;
				v = v * (unsigned)base + (unsigned)dv;
				digits = 1;
				c = inget(in);
				n++;
			}
			inunget(in, c);
			if (!digits) return c == EOF && !any ? EOF : assigned;
			if (neg) v = -v;
			any = 1;
			if (!skip) {
				*va_arg(ap, unsigned *) = v;
				assigned++;
			}
			continue;
		}
		return assigned;
	}
	return assigned;
}

int vsscanf(const char *s, const char *f, va_list ap) {
	In in;
	in.s = s;
	in.fp = NULL;
	in.count = 0;
	return doscanf(&in, f, ap);
}

int vfscanf(FILE *fp, const char *f, va_list ap) {
	In in;
	in.s = NULL;
	in.fp = fp;
	in.count = 0;
	return doscanf(&in, f, ap);
}

int sscanf(const char *s, const char *f, ...) {
	va_list ap;
	int r;
	va_start(ap, f);
	r = vsscanf(s, f, ap);
	va_end(ap);
	return r;
}

int fscanf(FILE *fp, const char *f, ...) {
	va_list ap;
	int r;
	va_start(ap, f);
	r = vfscanf(fp, f, ap);
	va_end(ap);
	return r;
}

int scanf(const char *f, ...) {
	va_list ap;
	int r;
	va_start(ap, f);
	r = vfscanf(stdin, f, ap);
	va_end(ap);
	return r;
}
