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
#include <poll.h>
#include <utime.h>
#include <sys/stat.h>

static int fails;

static void expect(const char *what, int got, int want) {
	if (got == want) printf("ok   %s\n", what);
	else {
		printf("FAIL %s: got %d want %d (errno %d)\n", what, got, want, errno);
		fails++;
	}
	fflush(stdout);
}

static void expect_err(const char *what, int r, int e) {
	if (r == -1 && errno == e) printf("ok   %s fails with errno %d\n", what, e);
	else {
		printf("FAIL %s: returned %d errno %d, want errno %d\n", what, r, errno, e);
		fails++;
	}
	fflush(stdout);
}

static int put(const char *path, const char *s) {
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
	int r;
	if (fd < 0) return -1;
	r = write(fd, s, strlen(s));
	close(fd);
	return r;
}

static int get(const char *path, char *buf, int n) {
	int fd = open(path, O_RDONLY), r;
	if (fd < 0) return -1;
	r = read(fd, buf, n - 1);
	close(fd);
	buf[r < 0 ? 0 : r] = 0;
	return r;
}

int main(void) {
	char buf[256];
	struct stat st, lst;
	int fd, fd2, p[2], r;
	struct pollfd pf[2];
	struct utimbuf ut;
	mkdir("/work", 0777);
	chdir("/work");
	expect("write a file", put("a.txt", "hello world\n"), 12);
	fd = open("a.txt", O_RDWR);
	expect("lseek end", lseek(fd, 0, SEEK_END), 12);
	expect("lseek set", lseek(fd, 6, SEEK_SET), 6);
	r = read(fd, buf, 5);
	buf[5] = 0;
	expect("read after lseek", strcmp(buf, "world"), 0);
	expect("lseek cur", lseek(fd, -3, SEEK_CUR), 8);
	expect_err("lseek before start", lseek(fd, -100, SEEK_CUR), EINVAL);
	expect("fstat size", (fstat(fd, &st), (int)st.st_size), 12);
	expect("fstat is regular", S_ISREG(st.st_mode), 1);
	expect("dup2", dup2(fd, 10), 10);
	expect("dup2 shares offset", lseek(10, 0, SEEK_CUR), 8);
	close(10);
	close(fd);
	pipe(p);
	expect_err("lseek on pipe", lseek(p[0], 0, SEEK_SET), ESPIPE);
	expect_err("lseek on console", lseek(0, 0, SEEK_SET), ESPIPE);
	expect("fstat pipe is fifo", (fstat(p[0], &st), S_ISFIFO(st.st_mode)), 1);
	fcntl(p[0], F_SETFL, O_NONBLOCK);
	expect("F_GETFL shows O_NONBLOCK", fcntl(p[0], F_GETFL) & O_NONBLOCK, O_NONBLOCK);
	expect_err("non blocking read of empty pipe", read(p[0], buf, 1), EAGAIN);
	pf[0].fd = p[0];
	pf[0].events = POLLIN;
	pf[1].fd = p[1];
	pf[1].events = POLLOUT;
	expect("poll: only the write end ready", poll(pf, 2, 0), 1);
	expect("poll revents of write end", pf[1].revents, POLLOUT);
	write(p[1], "x", 1);
	expect("poll after write", poll(pf, 1, 10), 1);
	expect("poll revents read end", pf[0].revents & POLLIN, POLLIN);
	expect("poll timeout on empty pipe", (read(p[0], buf, 1), poll(pf, 1, 5)), 0);
	close(p[1]);
	expect("poll sees hangup", (poll(pf, 1, 5), pf[0].revents & POLLHUP), POLLHUP);
	close(p[0]);
	expect_err("O_EXCL on existing file", open("a.txt", O_WRONLY | O_CREAT | O_EXCL, 0666), EEXIST);
	fd = open("new.txt", O_WRONLY | O_CREAT | O_EXCL, 0666);
	expect("O_EXCL creates", fd >= 0, 1);
	close(fd);
	expect_err("O_DIRECTORY on a file", open("a.txt", O_RDONLY | O_DIRECTORY), ENOTDIR);
	fd = open("a.txt", O_RDONLY | O_CLOEXEC);
	expect("O_CLOEXEC sets FD_CLOEXEC", fcntl(fd, F_GETFD), FD_CLOEXEC);
	fcntl(fd, F_SETFD, 0);
	expect("F_SETFD clears it", fcntl(fd, F_GETFD), 0);
	expect("F_DUPFD from 20", fcntl(fd, F_DUPFD, 20), 20);
	close(20);
	close(fd);
	expect("link", link("a.txt", "b.txt"), 0);
	expect("nlink after link", (stat("a.txt", &st), (int)st.st_nlink), 2);
	expect("same inode", (stat("b.txt", &lst), lst.st_ino == st.st_ino), 1);
	expect_err("link a directory", link("/work", "d2"), EPERM);
	expect_err("link across file systems", link("a.txt", "/tmp/x"), EXDEV);
	expect("unlink one name", unlink("a.txt"), 0);
	expect("nlink after unlink", (stat("b.txt", &st), (int)st.st_nlink), 1);
	expect("symlink", symlink("b.txt", "s.txt"), 0);
	expect("readlink length", readlink("s.txt", buf, sizeof(buf)), 5);
	expect("stat follows the link", (stat("s.txt", &st), S_ISREG(st.st_mode)), 1);
	expect("lstat does not", (lstat("s.txt", &st), S_ISLNK(st.st_mode)), 1);
	expect("read through link", (get("s.txt", buf, sizeof(buf)), strcmp(buf, "hello world\n")), 0);
	expect_err("readlink of a file", readlink("b.txt", buf, sizeof(buf)), EINVAL);
	symlink("loop2", "loop1");
	symlink("loop1", "loop2");
	expect_err("symlink loop", open("loop1", O_RDONLY), ELOOP);
	expect_err("O_NOFOLLOW on a link", open("s.txt", O_RDONLY | O_NOFOLLOW), ELOOP);
	symlink("/work/sub/deeper", "dirlink");
	mkdir("sub", 0777);
	mkdir("sub/deeper", 0777);
	put("sub/deeper/f", "deep\n");
	expect("path through a directory link", (get("dirlink/f", buf, sizeof(buf)), strcmp(buf, "deep\n")), 0);
	expect_err("rmdir of a non empty directory", rmdir("sub"), ENOTEMPTY);
	expect_err("rmdir of a file", rmdir("b.txt"), ENOTDIR);
	expect_err("rmdir of a mount point", rmdir("/tmp"), EBUSY);
	expect("rename in a directory", rename("b.txt", "c.txt"), 0);
	expect_err("old name gone", stat("b.txt", &st), ENOENT);
	expect("rename across directories", rename("c.txt", "sub/c.txt"), 0);
	expect("content after rename", (get("sub/c.txt", buf, sizeof(buf)), strcmp(buf, "hello world\n")), 0);
	expect("rename over an existing file", (put("x1", "one\n"), put("x2", "two\n"), rename("x1", "x2")), 0);
	expect("replaced content", (get("x2", buf, sizeof(buf)), strcmp(buf, "one\n")), 0);
	expect("rename a directory", rename("sub/deeper", "moved"), 0);
	expect("dotdot of moved directory", (chdir("moved"), get("../sub/c.txt", buf, sizeof(buf)) > 0), 1);
	chdir("/work");
	expect_err("rename into own subtree", rename("sub", "sub/inner"), EINVAL);
	expect_err("rename across file systems", rename("x2", "/tmp/x2"), EXDEV);
	expect("getcwd", strcmp(getcwd(buf, sizeof(buf)), "/work"), 0);
	expect_err("getcwd with a small buffer", getcwd(buf, 3) ? 0 : -1, ERANGE);
	chdir("moved");
	expect("getcwd in a subdirectory", strcmp(getcwd(buf, sizeof(buf)), "/work/moved"), 0);
	chdir("/tmp");
	expect("getcwd on a mounted file system", strcmp(getcwd(buf, sizeof(buf)), "/tmp"), 0);
	expect("dotdot leaves the mount", (chdir(".."), strcmp(getcwd(buf, sizeof(buf)), "/")), 0);
	chdir("/work");
	expect("truncate shorter", (truncate("x2", 2), stat("x2", &st), (int)st.st_size), 2);
	expect("truncate longer", (truncate("x2", 5000), stat("x2", &st), (int)st.st_size), 5000);
	fd = open("x2", O_RDWR);
	r = read(fd, buf, 4);
	expect("truncated file reads zeros", r == 4 && buf[0] == 'o' && buf[1] == 'n' && buf[2] == 0 && buf[3] == 0, 1);
	expect("ftruncate", (ftruncate(fd, 1), fstat(fd, &st), (int)st.st_size), 1);
	close(fd);
	expect_err("truncate a directory", truncate("sub", 0), EISDIR);
	ut.actime = 1000;
	ut.modtime = 2000;
	expect("utime", utime("x2", &ut), 0);
	expect("times after utime", (stat("x2", &st), st.st_atime == 1000 && st.st_mtime == 2000), 1);
	expect("utime NULL", utime("x2", NULL), 0);
	expect("utime NULL sets now", (stat("x2", &st), st.st_mtime != 2000), 1);
	fd = open("gone.txt", O_RDWR | O_CREAT, 0666);
	write(fd, "still here\n", 11);
	expect("unlink an open file", unlink("gone.txt"), 0);
	expect_err("name is gone", stat("gone.txt", &st), ENOENT);
	lseek(fd, 0, SEEK_SET);
	r = read(fd, buf, 11);
	buf[r > 0 ? r : 0] = 0;
	expect("still readable through the descriptor", strcmp(buf, "still here\n"), 0);
	expect("its link count is zero", (fstat(fd, &st), (int)st.st_nlink), 0);
	close(fd);
	expect("mount ramfs", (mkdir("/mnt2", 0777), mount("none", "/mnt2", "ramfs")), 0);
	expect("write on ramfs", put("/mnt2/r.txt", "ram\n"), 4);
	expect("ramfs device differs", (stat("/mnt2/r.txt", &st), stat("/work/x2", &lst), st.st_dev != lst.st_dev), 1);
	chdir("/mnt2");
	expect_err("umount while in use", umount("/mnt2"), EBUSY);
	chdir("/");
	expect("umount", umount("/mnt2"), 0);
	expect_err("file gone after umount", stat("/mnt2/r.txt", &st), ENOENT);
	expect_err("mount unknown type", mount("none", "/mnt2", "nosuch"), ENODEV);
	expect_err("mount on a file", mount("none", "/work/x2", "ramfs"), ENOTDIR);
	expect_err("umount of a directory that is not mounted", umount("/work"), EINVAL);
	expect("sync", sync(), 0);
	expect("/dev/null reads end of file", get("/dev/null", buf, sizeof(buf)), 0);
	fd = open("/dev/zero", O_RDONLY);
	buf[0] = 1;
	expect("/dev/zero reads zeros", read(fd, buf, 8) == 8 && buf[0] == 0 && buf[7] == 0, 1);
	close(fd);
	expect("/dev/null is a character device", (stat("/dev/null", &st), S_ISCHR(st.st_mode) && st.st_dev == 0x103), 1);
	expect_err("open a missing file", open("/work/missing", O_RDONLY), ENOENT);
	expect_err("path through a file", open("/work/x2/y", O_RDONLY), ENOTDIR);
	expect_err("name too long", open("/work/abcdefghijklmnopqrstuvwxyz0123456789", O_RDONLY), ENAMETOOLONG);
	printf("%s\n", fails ? "fstest: FAILED" : "fstest: all ok");
	return fails != 0;
}
