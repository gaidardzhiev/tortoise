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

static void put(const char *path, const char *s) {
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	write(fd, s, strlen(s));
	close(fd);
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

static void listing(const char *dir, char *out) {
	DIR *d = opendir(dir);
	struct dirent *e;
	out[0] = 0;
	if (!d) {
		strcpy(out, "(cannot open)");
		return;
	}
	while ((e = readdir(d)) != NULL) {
		if (out[0]) strcat(out, " ");
		strcat(out, e->d_name);
	}
	closedir(d);
}

static int child_sees(const char *path, const char *want) {
	char buf[64];
	int pid = fork(), st;
	if (pid == 0) _exit(get(path, buf, sizeof(buf)) > 0 && !strcmp(buf, want) ? 0 : 1);
	waitpid(pid, &st, 0);
	return WEXITSTATUS(st) == 0;
}

int main(void) {
	char buf[256], list[512];
	int pid, st, fd;
	struct stat sb;
	mkdir("/nst", 0755);
	mkdir("/nst/a", 0755);
	mkdir("/nst/b", 0755);
	mkdir("/nst/x", 0755);
	mkdir("/nst/x/y", 0755);
	put("/nst/a/onlya", "A\n");
	put("/nst/a/common", "from a\n");
	put("/nst/b/onlyb", "B\n");
	put("/nst/b/common", "from b\n");
	put("/nst/f1", "file one\n");
	put("/nst/f2", "file two\n");

	expect("rfork RFNAMEG in the caller", rfork(RFNAMEG) == 0);
	expect("bind -a makes a union", bind9("/nst/b", "/nst/a", MAFTER) == 0);
	expect("lookup finds a name of the first member", get("/nst/a/onlya", buf, sizeof(buf)) > 0 && !strcmp(buf, "A\n"));
	expect("lookup finds a name of the second member", get("/nst/a/onlyb", buf, sizeof(buf)) > 0 && !strcmp(buf, "B\n"));
	expect("the first member wins for a name in both", get("/nst/a/common", buf, sizeof(buf)) > 0 && !strcmp(buf, "from a\n"));
	listing("/nst/a", list);
	printf("union listing: %s\n", list);
	expect("readdir lists the union without duplicates", !strcmp(list, ". .. onlya common onlyb"));
	expect("create without MCREATE fails with EROFS", open("/nst/a/new", O_WRONLY | O_CREAT, 0644) == -1 && errno == EROFS);
	expect("mkdir without MCREATE fails with EROFS", mkdir("/nst/a/newdir", 0755) == -1 && errno == EROFS);
	expect("a child shares the namespace", child_sees("/nst/a/onlyb", "B\n"));
	fflush(stdout);
	pid = fork();
	if (pid == 0) {
		rfork(RFNAMEG);
		bind9("/nst/f2", "/nst/f1", MREPLACE);
		_exit(get("/nst/f1", buf, sizeof(buf)) > 0 && !strcmp(buf, "file two\n") ? 0 : 1);
	}
	waitpid(pid, &st, 0);
	expect("binding a file over a file in a private copy", WEXITSTATUS(st) == 0);
	expect("the parent does not see the child's bind", get("/nst/f1", buf, sizeof(buf)) > 0 && !strcmp(buf, "file one\n"));
	fflush(stdout);
	pid = rfork(RFPROC | RFFDG | RFNAMEG);
	if (pid == 0) {
		unmount(NULL, "/nst/a");
		_exit(get("/nst/a/onlyb", buf, sizeof(buf)) < 0 ? 0 : 1);
	}
	waitpid(pid, &st, 0);
	expect("rfork RFPROC|RFNAMEG: the child's unmount stays private", WEXITSTATUS(st) == 0 && get("/nst/a/onlyb", buf, sizeof(buf)) > 0);

	expect("unmount of one member", unmount("/nst/b", "/nst/a") == 0);
	expect("its names are gone", get("/nst/a/onlyb", buf, sizeof(buf)) < 0 && errno == ENOENT);
	expect("bind -b puts a member first", bind9("/nst/b", "/nst/a", MBEFORE) == 0 && get("/nst/a/common", buf, sizeof(buf)) > 0 && !strcmp(buf, "from b\n"));
	expect("unmount of the whole entry", unmount(NULL, "/nst/a") == 0 && get("/nst/a/onlyb", buf, sizeof(buf)) < 0 && get("/nst/a/common", buf, sizeof(buf)) > 0 && !strcmp(buf, "from a\n"));
	expect("unmount of a path that is not bound fails", unmount(NULL, "/nst/a") == -1 && errno == EINVAL);
	expect("bind -bc allows create", bind9("/nst/b", "/nst/a", MBEFORE | MCREATE) == 0);
	fd = open("/nst/a/created", O_WRONLY | O_CREAT, 0644);
	expect("create goes to the MCREATE member", fd >= 0 && stat("/nst/b/created", &sb) == 0);
	close(fd);
	unmount(NULL, "/nst/a");
	expect("bind replaces a directory", bind9("/nst/b", "/nst/a", MREPLACE) == 0 && get("/nst/a/onlyb", buf, sizeof(buf)) > 0 && get("/nst/a/onlya", buf, sizeof(buf)) < 0);
	unmount(NULL, "/nst/a");
	expect("bind of a file over a directory fails", bind9("/nst/f1", "/nst/a", 0) == -1 && errno == ENOTDIR);
	expect("bind of a directory over a file fails", bind9("/nst/a", "/nst/f1", 0) == -1 && errno == EISDIR);

	expect("bind a directory deeper in the tree", bind9("/nst/b", "/nst/x/y", MREPLACE) == 0);
	expect("chdir into the bound directory", chdir("/nst/x/y") == 0 && getcwd(buf, sizeof(buf)) && !strcmp(buf, "/nst/x/y"));
	expect("its files are b's", get("onlyb", buf, sizeof(buf)) > 0);
	memset(buf, 0, sizeof(buf));
	expect("/proc/self/cwd names the bound path", readlink("/proc/self/cwd", buf, sizeof(buf) - 1) > 0 && !strcmp(buf, "/nst/x/y"));
	expect("rmdir of a bind point fails with EBUSY", rmdir("/nst/x/y") == -1 && errno == EBUSY);
	fd = open("onlyb", O_RDONLY);
	expect("fd2path names the namespace path", fd >= 0 && fd2path(fd, buf, sizeof(buf)) == 0 && !strcmp(buf, "/nst/x/y/onlyb"));
	expect("fd2path with a small buffer fails", fd2path(fd, buf, 4) == -1 && errno == ERANGE);
	close(fd);
	expect(".. leaves the bind the way it came", chdir("..") == 0 && getcwd(buf, sizeof(buf)) && !strcmp(buf, "/nst/x"));
	expect("a path through the bind and .. too", stat("/nst/x/y/../y/onlyb", &sb) == 0);
	chdir("/");
	unmount(NULL, "/nst/x/y");

	fflush(stdout);
	pid = rfork(RFPROC | RFFDG | RFCNAMEG);
	if (pid == 0) {
		int ok = stat("/dev/null", &sb) == -1 && errno == ENOENT && stat("/bin/sh", &sb) == 0 && getcwd(buf, sizeof(buf)) && !strcmp(buf, "/");
		_exit(ok ? 0 : 1);
	}
	waitpid(pid, &st, 0);
	expect("RFCNAMEG: only the root, no /dev, cwd is /", WEXITSTATUS(st) == 0);
	expect("the caller still has /dev", stat("/dev/null", &sb) == 0);
	system("cp /bin/cat /nst/scat; chmod 4755 /nst/scat; cp /bin/ns /nst/sns; chmod 4755 /nst/sns");
	put("/nst/fakepw", "root::0:0:root:/root:/bin/sh\n");
	chmod("/nst", 0777);
	fflush(stdout);
	pid = fork();
	if (pid == 0) {
		setuid(100);
		rfork(RFNAMEG);
		if (bind9("/nst/fakepw", "/etc/passwd", MREPLACE) < 0 || chdir("/nst") < 0) _exit(2);
		if (system("head -1 /etc/passwd; ./scat /etc/passwd | head -1; ns | tail -1; ./sns; /bin/pwd") != 0) _exit(3);
		_exit(0);
	}
	waitpid(pid, &st, 0);
	expect("a user's bind is not seen by set user id programs", WEXITSTATUS(st) == 0);
	printf("%s\n", fails ? "nstest: FAILED" : "nstest: all ok");
	return fails != 0;
}
