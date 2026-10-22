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
#include <plan9.h>
#include <sys/stat.h>
#include <sys/wait.h>

static int fails;

static void expect(const char *what, int ok) {
	if (ok) printf("ok   %s\n", what);
	else {
		printf("FAIL %s (errno %d)\n", what, errno);
		fails++;
	}
	fflush(stdout);
}

static int put(const char *path, const char *s, int n) {
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644), r;
	if (fd < 0) return -1;
	r = write(fd, s, n);
	close(fd);
	return r;
}

static int get(const char *path, char *buf, int n) {
	int fd = open(path, O_RDONLY), r;
	buf[0] = 0;
	if (fd < 0) return -1;
	r = read(fd, buf, n - 1);
	close(fd);
	buf[r > 0 ? r : 0] = 0;
	return r;
}

static void listing(char *out) {
	DIR *d = opendir("/env");
	struct dirent *e;
	out[0] = 0;
	while (d && (e = readdir(d)) != NULL) {
		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
		if (out[0]) strcat(out, " ");
		strcat(out, e->d_name);
	}
	if (d) closedir(d);
}

static int child_status(int pid) {
	int st;
	waitpid(pid, &st, 0);
	return WIFEXITED(st) ? WEXITSTATUS(st) : 100;
}

int main(void) {
	char buf[256], list[512], *big;
	int pid, fd, i;
	struct stat sb;
	expect("write /env/greeting", put("/env/greeting", "hello", 5) == 5);
	expect("read it back", get("/env/greeting", buf, sizeof(buf)) == 5 && !strcmp(buf, "hello"));
	expect("its size", stat("/env/greeting", &sb) == 0 && sb.st_size == 5 && S_ISREG(sb.st_mode));
	expect("a list value keeps its zero bytes", put("/env/list", "a\0bc\0", 5) == 5 && get("/env/list", buf, sizeof(buf)) == 5 && !memcmp(buf, "a\0bc\0", 5));
	listing(list);
	printf("/env: %s\n", list);
	expect("getenv does not see /env", getenv("greeting") == NULL);
	setenv("POSIXONLY", "1", 1);
	expect("setenv does not write /env", stat("/env/POSIXONLY", &sb) < 0 && errno == ENOENT);
	expect("getenv still works", getenv("POSIXONLY") && !strcmp(getenv("POSIXONLY"), "1"));
	unsetenv("POSIXONLY");
	expect("unsetenv too", getenv("POSIXONLY") == NULL);
	pid = fork();
	if (pid == 0) _exit(put("/env/fromchild", "shared", 6) == 6 ? 0 : 1);
	expect("a child of fork shares the group", child_status(pid) == 0 && get("/env/fromchild", buf, sizeof(buf)) == 6);
	pid = rfork(RFPROC | RFFDG | RFENVG);
	if (pid == 0) {
		int ok = get("/env/greeting", buf, sizeof(buf)) == 5 && put("/env/greeting", "changed", 7) == 7 && put("/env/onlychild", "x", 1) == 1;
		_exit(ok ? 0 : 1);
	}
	expect("RFENVG: the child had a copy", child_status(pid) == 0);
	expect("its changes stay in the copy", get("/env/greeting", buf, sizeof(buf)) == 5 && !strcmp(buf, "hello") && stat("/env/onlychild", &sb) < 0);
	pid = rfork(RFPROC | RFFDG | RFCENVG);
	if (pid == 0) {
		listing(list);
		_exit(list[0] == 0 ? 0 : 1);
	}
	expect("RFCENVG: the child starts empty", child_status(pid) == 0);
	expect("RFENVG with RFCENVG is EINVAL", rfork(RFENVG | RFCENVG) < 0 && errno == EINVAL);
	expect("truncate a value", truncate("/env/greeting", 2) == 0 && get("/env/greeting", buf, sizeof(buf)) == 2 && !strcmp(buf, "he"));
	fd = open("/env/greeting", O_WRONLY | O_APPEND);
	expect("append to a value", fd >= 0 && write(fd, "llo!", 4) == 4);
	close(fd);
	expect("appended", get("/env/greeting", buf, sizeof(buf)) == 6 && !strcmp(buf, "hello!"));
	big = malloc(20000);
	for (i = 0; i < 20000; i++) big[i] = 'x';
	fd = open("/env/big", O_WRONLY | O_CREAT, 0644);
	expect("a value of 16384 bytes fits", fd >= 0 && write(fd, big, 16384) == 16384);
	expect("one more byte fails with EFBIG", write(fd, big, 1) < 0 && errno == EFBIG);
	close(fd);
	buf[0] = 0;
	errstr(buf, sizeof(buf));
	printf("big: errstr %s\n", buf);
	expect("remove a value", unlink("/env/big") == 0 && unlink("/env/fromchild") == 0 && stat("/env/fromchild", &sb) < 0);
	expect("rfork RFENVG on the caller", rfork(RFENVG) == 0 && put("/env/private", "p", 1) == 1);
	listing(list);
	printf("/env: %s\n", list);
	printf("%s\n", fails ? "envtest: FAILED" : "envtest: all ok");
	return fails != 0;
}
