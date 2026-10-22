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
#include <signal.h>
#include <dirent.h>
#include <plan9.h>
#include <sys/socket.h>
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
	if (fd >= 0) {
		write(fd, s, strlen(s));
		close(fd);
	}
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

static int server(const char *prog, const char *a1, const char *a2, int *pid) {
	int sv[2];
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) return -1;
	*pid = fork();
	if (*pid == 0) {
		close(sv[1]);
		dup2(sv[0], 0);
		dup2(sv[0], 1);
		close(sv[0]);
		execl(prog, prog, a1, a2, (char *)0);
		_exit(127);
	}
	close(sv[0]);
	return sv[1];
}

static void show_err(const char *what) {
	char buf[ERRMAX];
	buf[0] = 0;
	errstr(buf, sizeof(buf));
	printf("%s: errno %d, errstr %s\n", what, errno, buf);
	fflush(stdout);
}

int main(void) {
	char buf[512], want[512];
	int fd, pid, st, s;
	struct stat sb;
	expect("rfork RFNAMEG", rfork(RFNAMEG) == 0);
	mkdir("/n", 0755);
	mkdir("/n/a", 0755);
	mkdir("/n/e", 0755);
	mkdir("/n/s", 0755);

	fd = server("/bin/ramfs9", (char *)0, (char *)0, &pid);
	expect("mount9 of ramfs9 on a socket pair", fd >= 0 && mount9(fd, -1, "/n/a", MREPLACE | MCREATE, "") == 0);
	close(fd);
	put("/n/a/x", "on ramfs9\n");
	expect("a file written and read back", get("/n/a/x", buf, sizeof(buf)) > 0 && !strcmp(buf, "on ramfs9\n"));
	expect("its owner is the caller", stat("/n/a/x", &sb) == 0 && sb.st_uid == getuid() && S_ISREG(sb.st_mode) && sb.st_size == 10);
	expect("mkdir and rmdir", mkdir("/n/a/d", 0755) == 0 && stat("/n/a/d", &sb) == 0 && S_ISDIR(sb.st_mode) && rmdir("/n/a/d") == 0);
	expect("a missing file is ENOENT", open("/n/a/none", O_RDONLY) < 0 && errno == ENOENT);
	show_err("missing");
	expect("mount9 with authentication fails", mount9(0, 3, "/n/a", MREPLACE, "") < 0 && errno == EINVAL);
	show_err("afd");
	fd = open("/n/a/x", O_RDONLY);
	kill(pid, SIGKILL);
	waitpid(pid, &st, 0);
	expect("a server that dies gives EIO", read(fd, buf, 4) < 0 && errno == EIO);
	show_err("dead server");
	close(fd);
	expect("so does a new open", open("/n/a/x", O_RDONLY) < 0 && errno == EIO);
	expect("unmount of the dead mount", unmount(NULL, "/n/a") == 0);

	fd = server("/bin/exportfs", "-r", "/etc", &pid);
	expect("exportfs of /etc over a socket pair", fd >= 0 && mount9(fd, -1, "/n/e", MREPLACE, "") == 0);
	close(fd);
	get("/etc/motd", want, sizeof(want));
	expect("a file read through exportfs", get("/n/e/motd", buf, sizeof(buf)) > 0 && !strcmp(buf, want));
	expect("stat through exportfs", stat("/n/e/passwd", &sb) == 0 && S_ISREG(sb.st_mode) && sb.st_uid == 0);
	expect("creating without MCREATE fails", open("/n/e/new", O_WRONLY | O_CREAT, 0644) < 0 && errno == EROFS);
	unmount(NULL, "/n/e");
	kill(pid, SIGTERM);
	waitpid(pid, &st, 0);
	mkdir("/tmp/exp", 0777);
	fd = server("/bin/exportfs", "-r", "/tmp/exp", &pid);
	expect("exportfs of a directory with MCREATE", fd >= 0 && mount9(fd, -1, "/n/e", MREPLACE | MCREATE, "") == 0);
	close(fd);
	put("/n/e/made", "made remotely\n");
	expect("a file created through exportfs is there", get("/tmp/exp/made", buf, sizeof(buf)) > 0 && !strcmp(buf, "made remotely\n"));
	expect("rename through exportfs", rename("/n/e/made", "/n/e/renamed") == 0 && stat("/tmp/exp/renamed", &sb) == 0);
	expect("symlink through exportfs", symlink("renamed", "/n/e/link") == 0 && readlink("/tmp/exp/link", buf, sizeof(buf)) == 7);
	expect("remove through exportfs", unlink("/n/e/renamed") == 0 && stat("/tmp/exp/renamed", &sb) < 0);
	unmount(NULL, "/n/e");
	kill(pid, SIGTERM);
	waitpid(pid, &st, 0);

	s = server("/bin/ramfs9", (char *)0, (char *)0, &pid);
	fd = open("/srv/p9test", O_WRONLY | O_CREAT | O_EXCL, 0600);
	snprintf(buf, sizeof(buf), "%d", s);
	expect("post a descriptor in /srv", fd >= 0 && write(fd, buf, strlen(buf)) == (int)strlen(buf));
	close(fd);
	close(s);
	expect("a second post of the same name fails", open("/srv/p9test", O_WRONLY | O_CREAT | O_EXCL, 0600) < 0 && errno == EEXIST);
	fd = open("/srv/p9test", O_RDWR);
	expect("open of the post and mount9", fd >= 0 && mount9(fd, -1, "/n/s", MREPLACE | MCREATE, "srvtest") == 0);
	close(fd);
	put("/n/s/y", "through /srv\n");
	expect("file through the posted server", get("/n/s/y", buf, sizeof(buf)) > 0 && !strcmp(buf, "through /srv\n"));
	mkdir("/n/s2", 0755);
	fd = open("/srv/p9test", O_RDWR);
	expect("a second mount of the same connection", fd >= 0 && mount9(fd, -1, "/n/s2", MREPLACE, "") == 0);
	close(fd);
	expect("both mounts see the same tree", get("/n/s2/y", buf, sizeof(buf)) > 0 && !strcmp(buf, "through /srv\n"));
	put("/n/s/z", "second\n");
	expect("and keep working side by side", get("/n/s2/z", buf, sizeof(buf)) > 0 && get("/n/s/y", buf, sizeof(buf)) > 0);
	unmount(NULL, "/n/s2");
	expect("remove the post", unlink("/srv/p9test") == 0 && open("/srv/p9test", O_RDWR) < 0 && errno == ENOENT);
	expect("the mount still works", get("/n/s/y", buf, sizeof(buf)) > 0);
	fd = open("/proc/self/ns", O_RDONLY);
	buf[read(fd, buf, sizeof(buf) - 1)] = 0;
	close(fd);
	printf("%s", strstr(buf, "mount -c") ? strstr(buf, "mount -c") : buf);
	unmount(NULL, "/n/s");
	kill(pid, SIGTERM);
	waitpid(pid, &st, 0);
	printf("%s\n", fails ? "p9test: FAILED" : "p9test: all ok");
	return fails != 0;
}
