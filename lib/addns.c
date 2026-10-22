/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pwd.h>
#include <plan9.h>

#define MAXARG 8

static int tokens(char *line, char **argv, const char *user, const char *home, char *store, int max) {
	int n = 0, used = 0;
	char *p = line;
	while (*p && n < MAXARG) {
		char *w = store + used;
		int k = 0;
		while (*p == ' ' || *p == '\t') p++;
		if (!*p || *p == '\n') break;
		while (*p && *p != ' ' && *p != '\t' && *p != '\n') {
			if (*p == '\'') {
				p++;
				while (*p && *p != '\'') {
					if (used + k < max - 1) w[k++] = *p;
					p++;
				}
				if (*p) p++;
			} else if (*p == '$') {
				const char *val = NULL;
				int l = 0;
				if (!strncmp(p + 1, "user", 4)) {
					val = user;
					l = 5;
				} else if (!strncmp(p + 1, "home", 4)) {
					val = home;
					l = 5;
				}
				if (val) {
					while (*val && used + k < max - 1) w[k++] = *val++;
					p += l;
				} else {
					if (used + k < max - 1) w[k++] = *p;
					p++;
				}
			} else {
				if (used + k < max - 1) w[k++] = *p;
				p++;
			}
		}
		w[k] = 0;
		argv[n++] = w;
		used += k + 1;
	}
	return n;
}

static int flags_of(char **argv, int n, int *i) {
	int fl = MREPLACE;
	*i = 1;
	while (*i < n && argv[*i][0] == '-' && argv[*i][1] && strcmp(argv[*i], "-t")) {
		char *o;
		for (o = argv[*i] + 1; *o; o++) {
			if (*o == 'b') fl = (fl & ~3) | MBEFORE;
			else if (*o == 'a') fl = (fl & ~3) | MAFTER;
			else if (*o == 'c') fl |= MCREATE;
			else if (*o != 'C') return -1;
		}
		(*i)++;
	}
	return fl;
}

static int apply(const char *user, const char *home, const char *file, int depth) {
	char *buf, *line, store[1024], *argv[MAXARG];
	int fd, n, cap = 4096, len = 0, bad = 0;
	if (depth > 8) return -1;
	fd = open(file, O_RDONLY);
	if (fd < 0) return -1;
	buf = malloc((unsigned)cap);
	if (!buf) {
		close(fd);
		return -1;
	}
	while ((n = read(fd, buf + len, (unsigned)(cap - len - 1))) > 0) {
		len += n;
		if (len + 1 >= cap) {
			cap *= 2;
			buf = realloc(buf, (unsigned)cap);
		}
	}
	close(fd);
	buf[len] = 0;
	for (line = buf; *line;) {
		char *end = strchr(line, '\n');
		int i, fl, argc;
		if (end) *end = 0;
		argc = tokens(line, argv, user, home, store, sizeof(store));
		line = end ? end + 1 : line + strlen(line);
		if (argc == 0 || argv[0][0] == '#') continue;
		if (!strcmp(argv[0], "bind")) {
			fl = flags_of(argv, argc, &i);
			if (fl < 0 || argc - i != 2 || bind9(argv[i], argv[i + 1], fl) < 0) bad = 1;
		} else if (!strcmp(argv[0], "mount")) {
			fl = flags_of(argv, argc, &i);
			if (fl >= 0 && i < argc && !strcmp(argv[i], "-t") && argc - i == 4) {
				if (mount(argv[i + 2], argv[i + 3], argv[i + 1]) < 0) bad = 1;
			} else if (fl >= 0 && (argc - i == 2 || argc - i == 3)) {
				int s = open(argv[i], O_RDWR);
				if (s < 0 || mount9(s, -1, argv[i + 1], fl, argc - i == 3 ? argv[i + 2] : "") < 0) bad = 1;
				if (s >= 0) close(s);
			} else bad = 1;
		} else if (!strcmp(argv[0], "unmount")) {
			if (argc == 2) {
				if (unmount(NULL, argv[1]) < 0) bad = 1;
			} else if (argc == 3) {
				if (unmount(argv[1], argv[2]) < 0) bad = 1;
			} else bad = 1;
		} else if (!strcmp(argv[0], "cd")) {
			if (argc != 2 || chdir(argv[1]) < 0) bad = 1;
		} else if (!strcmp(argv[0], ".")) {
			if (argc != 2 || apply(user, home, argv[1], depth + 1) < 0) bad = 1;
		} else if (!strcmp(argv[0], "clear")) {
			if (rfork(RFCNAMEG) < 0) bad = 1;
		} else bad = 1;
		if (bad == 1) {
			fprintf(stderr, "namespace %s: cannot do: %s\n", file, argv[0]);
			bad = 2;
		}
	}
	free(buf);
	return bad ? -1 : 0;
}

int addns(const char *user, const char *file) {
	char u[64], home[128];
	struct passwd *pw;
	strncpy(u, user, sizeof(u) - 1);
	u[sizeof(u) - 1] = 0;
	pw = getpwnam(u);
	strncpy(home, pw && pw->pw_dir[0] ? pw->pw_dir : "/", sizeof(home) - 1);
	home[sizeof(home) - 1] = 0;
	return apply(u, home, file, 0);
}
