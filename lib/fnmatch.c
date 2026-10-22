/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <string.h>
#include <fnmatch.h>

static const char *bracket(const char *p, int c, int *ok) {
	int neg = 0, hit = 0;
	if (*p == '!' || *p == '^') {
		neg = 1;
		p++;
	}
	if (*p == ']') {
		hit = c == ']';
		p++;
	}
	while (*p && *p != ']') {
		if (p[1] == '-' && p[2] && p[2] != ']') {
			if (c >= (unsigned char)p[0] && c <= (unsigned char)p[2]) hit = 1;
			p += 3;
		} else {
			if (c == (unsigned char)*p) hit = 1;
			p++;
		}
	}
	if (*p != ']') return NULL;
	*ok = hit != neg;
	return p + 1;
}

static int match(const char *p, const char *s, int flags, int start) {
	for (;;) {
		int c = (unsigned char)*s;
		if ((flags & FNM_PERIOD) && start && c == '.' && *p != '.') return FNM_NOMATCH;
		start = 0;
		switch (*p) {
		case 0:
			return c ? FNM_NOMATCH : 0;
		case '?':
			if (!c || ((flags & FNM_PATHNAME) && c == '/')) return FNM_NOMATCH;
			p++;
			s++;
			break;
		case '*':
			while (*p == '*') p++;
			if (!*p) return (flags & FNM_PATHNAME) && strchr(s, '/') ? FNM_NOMATCH : 0;
			for (; *s; s++) {
				if (match(p, s, flags, 0) == 0) return 0;
				if ((flags & FNM_PATHNAME) && *s == '/') return FNM_NOMATCH;
			}
			return match(p, s, flags, 0);
		case '[': {
			int ok = 0;
			const char *q;
			if (!c || ((flags & FNM_PATHNAME) && c == '/')) return FNM_NOMATCH;
			q = bracket(p + 1, c, &ok);
			if (!q) {
				if (c != '[') return FNM_NOMATCH;
				p++;
				s++;
				break;
			}
			if (!ok) return FNM_NOMATCH;
			p = q;
			s++;
			break;
		}
		case '\\':
			if (!(flags & FNM_NOESCAPE) && p[1]) p++;
		default:
			if (c != (unsigned char)*p) return FNM_NOMATCH;
			p++;
			s++;
			if ((flags & FNM_PATHNAME) && c == '/') start = 1;
			break;
		}
	}
}

int fnmatch(const char *pattern, const char *s, int flags) {
	return match(pattern, s, flags, 1);
}
