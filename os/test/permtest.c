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
#include <sys/stat.h>
#include <sys/wait.h>

#define A 1000
#define B 1001
#define C 1002
#define G 100

static int fails;

static void expect(const char *what, int got, int want) {
	if (got == want) printf("ok   %s\n", what);
	else {
		printf("FAIL %s: got %d want %d (errno %d)\n", what, got, want, errno);
		fails++;
	}
	fflush(stdout);
}

static void become(int uid, int gid) {
	setgroups(0, NULL);
	setgid(gid);
	setuid(uid);
}

static int as(int uid, int gid, int (*fn)(void)) {
	int pid, st;
	fflush(stdout);
	pid = fork();
	if (pid == 0) {
		if (uid) become(uid, gid);
		_exit(fn());
	}
	waitpid(pid, &st, 0);
	return WEXITSTATUS(st);
}

static int can_read(void) {
	int fd = open("/perm/f", O_RDONLY);
	if (fd < 0) return 0;
	close(fd);
	return 1;
}

static int can_write(void) {
	int fd = open("/perm/f", O_WRONLY);
	if (fd < 0) return 0;
	close(fd);
	return 1;
}

static int can_exec(void) {
	return access("/perm/f", X_OK) == 0;
}

static int rwx(void) {
	return can_read() * 4 + can_write() * 2 + can_exec();
}

static void matrix(unsigned mode) {
	static const char *who[] = {"owner", "group", "other", "root"};
	int uids[4], gids[4], i;
	uids[0] = A;
	gids[0] = G;
	uids[1] = B;
	gids[1] = G;
	uids[2] = C;
	gids[2] = 200;
	uids[3] = 0;
	gids[3] = 0;
	chmod("/perm/f", mode);
	printf("mode %04o:", mode);
	for (i = 0; i < 4; i++) {
		int r = as(uids[i], gids[i], rwx);
		printf(" %s %c%c%c", who[i], r & 4 ? 'r' : '-', r & 2 ? 'w' : '-', r & 1 ? 'x' : '-');
	}
	printf("\n");
}

static int search_denied(void) {
	int fd = open("/perm/private/inside", O_RDONLY);
	return fd < 0 && errno == EACCES;
}

static int create_denied(void) {
	int fd = open("/perm/private2/new", O_WRONLY | O_CREAT, 0666);
	return fd < 0 && errno == EACCES;
}

static int a_makes_file(void) {
	int fd = open("/perm/tmp/a-file", O_WRONLY | O_CREAT, 0666);
	if (fd < 0) return 0;
	close(fd);
	fd = open("/perm/tmp/a-file2", O_WRONLY | O_CREAT, 0666);
	if (fd < 0) return 0;
	close(fd);
	return 1;
}

static int b_unlinks_a(void) {
	return unlink("/perm/tmp/a-file") == -1 && errno == EPERM;
}

static int b_renames_a(void) {
	return rename("/perm/tmp/a-file", "/perm/tmp/b-name") == -1 && errno == EPERM;
}

static int b_own_file(void) {
	int fd = open("/perm/tmp/b-file", O_WRONLY | O_CREAT, 0666);
	if (fd < 0) return 0;
	close(fd);
	return unlink("/perm/tmp/b-file") == 0;
}

static int a_unlinks_own(void) {
	return unlink("/perm/tmp/a-file2") == 0;
}

static int chown_denied(void) {
	return chown("/perm/mine", B, G) == -1 && errno == EPERM;
}

static int chmod_by_owner(void) {
	return chmod("/perm/mine", 0640) == 0;
}

static int chmod_by_other(void) {
	return chmod("/perm/mine", 0777) == -1 && errno == EPERM;
}

static int umask_applies(void) {
	struct stat st;
	int fd;
	umask(077);
	fd = open("/perm/tmp/masked", O_WRONLY | O_CREAT, 0666);
	close(fd);
	stat("/perm/tmp/masked", &st);
	unlink("/perm/tmp/masked");
	return (st.st_mode & 0777) == 0600 && st.st_uid == A && st.st_gid == G;
}

static int setgid_dir(void) {
	struct stat st;
	int fd = open("/perm/shared/file", O_WRONLY | O_CREAT, 0666);
	close(fd);
	stat("/perm/shared/file", &st);
	return st.st_gid == 300;
}

static int back_to_root(void) {
	return setuid(0) == -1 && errno == EPERM;
}

static int mount_denied(void) {
	return mount("none", "/perm", "ramfs") == -1 && errno == EPERM;
}

static int setreuid_swap(void) {
	setreuid(A, B);
	return getuid() == A && geteuid() == B && setreuid(B, A) == 0 && getuid() == B && geteuid() == A;
}

static int kill_target;

static int kill_other(void) {
	return kill(kill_target, SIGTERM) == -1 && errno == EPERM;
}

static int kill_own(void) {
	return kill(kill_target, 0) == 0;
}

static int run_suid(void) {
	char *argv[2];
	argv[0] = "id";
	argv[1] = NULL;
	execv("/perm/suid-id", argv);
	return 99;
}

static void copy(const char *from, const char *to) {
	char buf[512];
	int in = open(from, O_RDONLY), out = open(to, O_WRONLY | O_CREAT | O_TRUNC, 0666), n;
	while ((n = read(in, buf, sizeof(buf))) > 0) write(out, buf, n);
	close(in);
	close(out);
}

int main(void) {
	int fd, pid, st;
	unsigned modes[] = {0400, 0200, 0100, 0040, 0020, 0010, 0004, 0002, 0001, 0640, 0750, 0000};
	unsigned i;
	struct stat sb;
	umask(0);
	mkdir("/perm", 0777);
	chmod("/perm", 0755);
	fd = open("/perm/f", O_WRONLY | O_CREAT, 0666);
	close(fd);
	chown("/perm/f", A, G);
	for (i = 0; i < sizeof(modes) / sizeof(modes[0]); i++) matrix(modes[i]);

	mkdir("/perm/private", 0777);
	fd = open("/perm/private/inside", O_WRONLY | O_CREAT, 0666);
	close(fd);
	chmod("/perm/private/inside", 0644);
	chown("/perm/private", A, G);
	chmod("/perm/private", 0700);
	expect("a directory without x for others stops the lookup", as(B, G, search_denied), 1);
	mkdir("/perm/private2", 0777);
	chown("/perm/private2", A, G);
	chmod("/perm/private2", 0755);
	expect("creating in a directory without w is EACCES", as(B, G, create_denied), 1);

	mkdir("/perm/tmp", 0777);
	chmod("/perm/tmp", 01777);
	expect("anyone creates in a sticky directory", as(A, G, a_makes_file), 1);
	expect("sticky: another user cannot unlink it", as(B, G, b_unlinks_a), 1);
	expect("sticky: nor rename it", as(B, G, b_renames_a), 1);
	expect("sticky: a user removes its own file", as(B, G, b_own_file), 1);
	expect("sticky: the owner removes its file", as(A, G, a_unlinks_own), 1);
	expect("sticky: root removes anything", unlink("/perm/tmp/a-file"), 0);

	fd = open("/perm/mine", O_WRONLY | O_CREAT, 0666);
	close(fd);
	chown("/perm/mine", A, G);
	expect("chown is root only", as(A, G, chown_denied), 1);
	expect("the owner may chmod", as(A, G, chmod_by_owner), 1);
	expect("others may not chmod", as(B, G, chmod_by_other), 1);
	chmod("/perm/mine", 06755);
	chown("/perm/mine", B, G);
	stat("/perm/mine", &sb);
	expect("chown clears set user and group id", (int)(sb.st_mode & 07777), 0755);
	expect("umask and the creator's ids", as(A, G, umask_applies), 1);
	mkdir("/perm/shared", 0777);
	chown("/perm/shared", 0, 300);
	chmod("/perm/shared", 02777);
	expect("a setgid directory gives its group", as(A, G, setgid_dir), 1);
	expect("a user cannot setuid back to root", as(A, G, back_to_root), 1);
	expect("mount is root only", as(A, G, mount_denied), 1);
	expect("setreuid swaps real and effective", as(0, 0, setreuid_swap), 1);

	pid = fork();
	if (pid == 0) {
		become(B, G);
		for (;;) pause();
	}
	usleep(5 * 10000);
	kill_target = pid;
	expect("kill another user's process: EPERM", as(A, G, kill_other), 1);
	expect("kill a process of the same user", as(B, G, kill_own), 1);
	expect("root may kill it", kill(pid, SIGKILL), 0);
	waitpid(pid, &st, 0);

	copy("/bin/id", "/perm/suid-id");
	chown("/perm/suid-id", B, G);
	chmod("/perm/suid-id", 04755);
	printf("a set user id program run by user %d:\n", A);
	as(A, G, run_suid);
	copy("/bin/id", "/perm/noexec");
	chmod("/perm/noexec", 0644);
	expect("root cannot exec a file without x bits", execv("/perm/noexec", NULL) == -1 && errno == EACCES, 1);
	expect("access uses the real ids", access("/perm/private/inside", R_OK), 0);
	printf("%s\n", fails ? "permtest: FAILED" : "permtest: all ok");
	return fails != 0;
}
