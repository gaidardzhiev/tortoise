/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

static char **t_argv;
static int t_argc, t_pos, t_err;

static const char *t_peek(void) {
	return t_pos < t_argc ? t_argv[t_pos] : NULL;
}

static int t_num(const char *s, int *v) {
	const char *p = s;
	if (*p == '-' || *p == '+') p++;
	if (!*p) return 0;
	for (; *p; p++)
		if (*p < '0' || *p > '9') return 0;
	*v = atoi(s);
	return 1;
}

static int t_unary(const char *op, const char *a) {
	struct stat st;
	char c = op[1];
	if (c == 'n') return a[0] != 0;
	if (c == 'z') return a[0] == 0;
	if (c == 'r') return access(a, R_OK) == 0;
	if (c == 'w') return access(a, W_OK) == 0;
	if (c == 'x') return access(a, X_OK) == 0;
	if (c == 't') return isatty(atoi(a));
	if (c == 'L' || c == 'h') return lstat(a, &st) == 0 && S_ISLNK(st.st_mode);
	if (stat(a, &st) < 0) return 0;
	if (c == 'e') return 1;
	if (c == 'f') return S_ISREG(st.st_mode);
	if (c == 'd') return S_ISDIR(st.st_mode);
	if (c == 'c') return S_ISCHR(st.st_mode);
	if (c == 'p') return S_ISFIFO(st.st_mode);
	if (c == 's') return st.st_size > 0;
	if (c == 'u') return (st.st_mode & 04000) != 0;
	if (c == 'g') return (st.st_mode & 02000) != 0;
	if (c == 'k') return (st.st_mode & 01000) != 0;
	t_err = 1;
	return 0;
}

static int t_is_unary(const char *s) {
	return s && s[0] == '-' && s[1] && !s[2] && strchr("nzrwxtLhefdcpsugk", s[1]);
}

static int t_is_binary(const char *s) {
	static const char *ops[] = {"=", "!=", "-eq", "-ne", "-lt", "-le", "-gt", "-ge", NULL};
	int i;
	if (!s) return 0;
	for (i = 0; ops[i]; i++)
		if (!strcmp(s, ops[i])) return 1;
	return 0;
}

static int t_or(void);

static int t_primary(void) {
	const char *a = t_peek(), *op;
	if (!a) {
		t_err = 1;
		return 0;
	}
	if (!strcmp(a, "!")) {
		t_pos++;
		return !t_primary();
	}
	if (!strcmp(a, "(")) {
		int r;
		t_pos++;
		r = t_or();
		if (!t_peek() || strcmp(t_peek(), ")")) t_err = 1;
		else t_pos++;
		return r;
	}
	if (t_is_unary(a) && t_pos + 1 < t_argc && !t_is_binary(t_pos + 2 < t_argc ? t_argv[t_pos + 2] : NULL)) {
		t_pos += 2;
		return t_unary(a, t_argv[t_pos - 1]);
	}
	t_pos++;
	op = t_peek();
	if (t_is_binary(op) && t_pos + 1 < t_argc) {
		const char *b = t_argv[t_pos + 1];
		int x, y;
		t_pos += 2;
		if (!strcmp(op, "=")) return strcmp(a, b) == 0;
		if (!strcmp(op, "!=")) return strcmp(a, b) != 0;
		if (!t_num(a, &x) || !t_num(b, &y)) {
			t_err = 1;
			return 0;
		}
		if (!strcmp(op, "-eq")) return x == y;
		if (!strcmp(op, "-ne")) return x != y;
		if (!strcmp(op, "-lt")) return x < y;
		if (!strcmp(op, "-le")) return x <= y;
		if (!strcmp(op, "-gt")) return x > y;
		return x >= y;
	}
	return a[0] != 0;
}

static int t_and(void) {
	int r = t_primary();
	while (t_peek() && !strcmp(t_peek(), "-a")) {
		int s;
		t_pos++;
		s = t_primary();
		r = r && s;
	}
	return r;
}

static int t_or(void) {
	int r = t_and();
	while (t_peek() && !strcmp(t_peek(), "-o")) {
		int s;
		t_pos++;
		s = t_and();
		r = r || s;
	}
	return r;
}

static int test_main(int argc, char **argv) {
	int r;
	if (!strcmp(argv[0], "[") || (strlen(argv[0]) > 2 && !strcmp(argv[0] + strlen(argv[0]) - 2, "/["))) {
		if (argc < 2 || strcmp(argv[argc - 1], "]")) {
			fprintf(stderr, "[: missing ]\n");
			return 2;
		}
		argc--;
	}
	t_argv = argv;
	t_argc = argc;
	t_pos = 1;
	t_err = 0;
	if (argc == 1) return 1;
	r = t_or();
	if (t_err || t_pos != t_argc) {
		fprintf(stderr, "test: bad expression\n");
		return 2;
	}
	return !r;
}
