/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <fnmatch.h>
#include <glob.h>
#include <sys/wait.h>
#include <sys/stat.h>

DIR *opendir(const char *path) {
	struct stat st;
	DIR *d;
	int fd = open(path, O_RDONLY | O_DIRECTORY);
	if (fd < 0) return NULL;
	if (fstat(fd, &st) < 0 || !S_ISDIR(st.st_mode)) {
		close(fd);
		errno = ENOTDIR;
		return NULL;
	}
	d = malloc(sizeof(DIR));
	if (!d) {
		close(fd);
		errno = ENOMEM;
		return NULL;
	}
	d->fd = fd;
	fcntl(fd, F_SETFD, FD_CLOEXEC);
	return d;
}

struct dirent *readdir(DIR *d) {
	return getdirent(d->fd, &d->ent) > 0 ? &d->ent : NULL;
}

int closedir(DIR *d) {
	int r = close(d->fd);
	free(d);
	return r;
}

void rewinddir(DIR *d) {
	lseek(d->fd, 0, SEEK_SET);
}

int dirfd(DIR *d) {
	return d->fd;
}

static int cmpstr(const void *a, const void *b) {
	return strcmp(*(char *const *)a, *(char *const *)b);
}

static int add_path(glob_t *g, const char *s, size_t *cap) {
	if (g->gl_pathc + 1 >= *cap) {
		char **n = realloc(g->gl_pathv, (*cap * 2 + 8) * sizeof(char *));
		if (!n) return -1;
		g->gl_pathv = n;
		*cap = *cap * 2 + 8;
	}
	g->gl_pathv[g->gl_pathc] = strdup(s);
	if (!g->gl_pathv[g->gl_pathc]) return -1;
	g->gl_pathc++;
	g->gl_pathv[g->gl_pathc] = NULL;
	return 0;
}

int glob(const char *pattern, int flags, int (*errfunc)(const char *, int), glob_t *g) {
	char dir[256], path[512];
	const char *slash = strrchr(pattern, '/'), *pat;
	size_t cap = 0, first;
	DIR *d;
	struct dirent *e;
	(void)errfunc;
	g->gl_pathc = 0;
	g->gl_pathv = NULL;
	g->gl_offs = 0;
	if (slash) {
		size_t n = (size_t)(slash - pattern);
		if (n >= sizeof(dir)) n = sizeof(dir) - 1;
		memcpy(dir, pattern, n);
		dir[n] = 0;
		if (n == 0) strcpy(dir, "/");
		pat = slash + 1;
	} else {
		strcpy(dir, ".");
		pat = pattern;
	}
	first = 0;
	d = strpbrk(pat, "*?[") ? opendir(dir) : NULL;
	if (d) {
		while ((e = readdir(d)) != NULL) {
			if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
			if (fnmatch(pat, e->d_name, FNM_PERIOD) != 0) continue;
			if (slash) snprintf(path, sizeof(path), "%s%s%s", slash == pattern ? "" : dir, "/", e->d_name);
			else snprintf(path, sizeof(path), "%s", e->d_name);
			if (add_path(g, path, &cap) < 0) {
				closedir(d);
				return GLOB_NOSPACE;
			}
		}
		closedir(d);
	} else if (!strpbrk(pat, "*?[")) {
		struct stat st;
		if (lstat(pattern, &st) == 0 && add_path(g, pattern, &cap) < 0) return GLOB_NOSPACE;
	}
	if (g->gl_pathc == first) {
		if (!(flags & GLOB_NOCHECK)) return GLOB_NOMATCH;
		if (add_path(g, pattern, &cap) < 0) return GLOB_NOSPACE;
		return 0;
	}
	qsort(g->gl_pathv, g->gl_pathc, sizeof(char *), cmpstr);
	return 0;
}

void globfree(glob_t *g) {
	size_t i;
	for (i = 0; i < g->gl_pathc; i++) free(g->gl_pathv[i]);
	free(g->gl_pathv);
	g->gl_pathv = NULL;
	g->gl_pathc = 0;
}

int system(const char *cmd) {
	int pid, st;
	char *argv[4];
	if (!cmd) return 1;
	pid = fork();
	if (pid < 0) return -1;
	if (pid == 0) {
		argv[0] = "sh";
		argv[1] = "-c";
		argv[2] = (char *)cmd;
		argv[3] = NULL;
		execv("/bin/sh", argv);
		_exit(127);
	}
	while (waitpid(pid, &st, 0) < 0)
		if (errno != EINTR) return -1;
	return st;
}

int mkstemp(char *tmpl) {
	static unsigned seq;
	int n = (int)strlen(tmpl), i, tries;
	if (n < 6 || strcmp(tmpl + n - 6, "XXXXXX")) {
		errno = EINVAL;
		return -1;
	}
	for (tries = 0; tries < 1000; tries++) {
		unsigned v = (unsigned)getpid() * 7919u + seq++ * 104729u;
		int fd;
		for (i = 0; i < 6; i++) {
			tmpl[n - 6 + i] = "abcdefghijklmnopqrstuvwxyz0123456789"[v % 36];
			v /= 36;
		}
		fd = open(tmpl, O_RDWR | O_CREAT | O_EXCL, 0600);
		if (fd >= 0) return fd;
		if (errno != EEXIST) return -1;
	}
	return -1;
}

FILE *tmpfile(void) {
	char name[32];
	int fd;
	FILE *fp;
	strcpy(name, "/tmp/tmpXXXXXX");
	fd = mkstemp(name);
	if (fd < 0) return NULL;
	unlink(name);
	fp = fdopen(fd, "w+");
	if (!fp) close(fd);
	return fp;
}
