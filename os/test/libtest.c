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
#include <glob.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/time.h>
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

static int child_status(void (*fn)(void)) {
	int pid, st;
	fflush(stdout);
	pid = fork();
	if (pid == 0) {
		fn();
		_exit(0);
	}
	waitpid(pid, &st, 0);
	return st;
}

static void do_execl(void) {
	execl("/bin/sh", "sh", "-c", "exit 4", (char *)NULL);
	_exit(99);
}

static void do_execlp(void) {
	setenv("PATH", "/nowhere:/bin", 1);
	execlp("sh", "sh", "-c", "exit 5", (char *)NULL);
	_exit(99);
}

static void do_execle(void) {
	char *env[2];
	env[0] = "CODE=6";
	env[1] = NULL;
	execle("/bin/sh", "sh", "-c", "exit $CODE", (char *)NULL, env);
	_exit(99);
}

static void do_abort(void) {
	abort();
}

int main(void) {
	FILE *f, *g;
	char buf[128], *line = NULL, name[32];
	size_t cap = 0;
	int fd, n, a, b, found;
	struct stat st;
	DIR *d;
	struct dirent *e;
	glob_t gl;
	struct passwd *pw;
	struct group *gr;
	struct timeval tv;
	time_t t;
	void *p0, *p1;

	f = fopen("/tmp/lib.txt", "w+");
	fputs("first line\nsecond 12 34\nthird\n", f);
	expect("ftell after writing", ftell(f) == 30);
	rewind(f);
	expect("rewind and getline", getline(&line, &cap, f) == 11 && !strcmp(line, "first line\n"));
	expect("fscanf", fscanf(f, "%s %d %d", buf, &a, &b) == 3 && !strcmp(buf, "second") && a == 12 && b == 34);
	expect("fseek SEEK_SET", fseek(f, 6, SEEK_SET) == 0 && fgetc(f) == 'l');
	expect("fseek SEEK_CUR", fseek(f, 2, SEEK_CUR) == 0 && ftell(f) == 9);
	expect("fseek SEEK_END", fseek(f, -6, SEEK_END) == 0 && getline(&line, &cap, f) == 6 && !strcmp(line, "third\n"));
	expect("getline at end of file", getline(&line, &cap, f) == -1 && feof(f));
	expect("fileno", fileno(f) >= 3 && fileno(stdin) == 0 && fileno(stdout) == 1);
	fclose(f);

	fd = open("/tmp/lib.txt", O_RDONLY);
	g = fdopen(fd, "r");
	expect("fdopen", g && fgets(buf, sizeof(buf), g) && !strcmp(buf, "first line\n"));
	fclose(g);
	expect("fclose of fdopen closes the descriptor", read(fd, buf, 1) == -1 && errno == EBADF);

	f = fopen("/tmp/vbuf.txt", "w");
	setvbuf(f, NULL, _IONBF, 0);
	fputs("x", f);
	expect("setvbuf unbuffered writes at once", stat("/tmp/vbuf.txt", &st) == 0 && st.st_size == 1);
	setvbuf(f, NULL, _IOFBF, BUFSIZ);
	fputs("yz", f);
	expect("setvbuf fully buffered holds output", stat("/tmp/vbuf.txt", &st) == 0 && st.st_size == 1);
	setbuf(f, NULL);
	expect("setbuf NULL flushes and unbuffers", stat("/tmp/vbuf.txt", &st) == 0 && st.st_size == 3);
	fclose(f);

	f = fopen("/tmp/redir.txt", "w");
	fclose(f);
	fflush(stdout);
	fd = dup(1);
	g = freopen("/tmp/redir.txt", "w", stdout);
	printf("into the file\n");
	fflush(stdout);
	dup2(fd, 1);
	close(fd);
	expect("freopen stdout", g == stdout);
	f = fopen("/tmp/redir.txt", "r");
	expect("freopen kept descriptor 1", fgets(buf, sizeof(buf), f) && !strcmp(buf, "into the file\n"));
	fclose(f);

	f = tmpfile();
	expect("tmpfile", f != NULL);
	fputs("temporary", f);
	rewind(f);
	expect("tmpfile reads back", fgets(buf, sizeof(buf), f) && !strcmp(buf, "temporary"));
	fclose(f);
	strcpy(buf, "/tmp/mkXXXXXX");
	fd = mkstemp(buf);
	expect("mkstemp", fd >= 0 && strncmp(buf, "/tmp/mk", 7) == 0 && strcmp(buf + 7, "XXXXXX") != 0);
	expect("mkstemp mode 0600", fstat(fd, &st) == 0 && (st.st_mode & 0777) == 0600);
	close(fd);
	strcpy(name, buf);
	strcpy(buf, "/tmp/mkXXXXXX");
	fd = mkstemp(buf);
	expect("mkstemp names differ", fd >= 0 && strcmp(buf, name) != 0);
	close(fd);
	expect("mkstemp needs XXXXXX", mkstemp(strcpy(buf, "/tmp/bad")) == -1 && errno == EINVAL);

	n = system("exit 3");
	expect("system", WIFEXITED(n) && WEXITSTATUS(n) == 3);
	n = system("echo from system");
	expect("system runs a command", n == 0);

	mkdir("/tmp/dir", 0755);
	close(open("/tmp/dir/b.c", O_WRONLY | O_CREAT, 0644));
	close(open("/tmp/dir/a.c", O_WRONLY | O_CREAT, 0644));
	close(open("/tmp/dir/c.h", O_WRONLY | O_CREAT, 0644));
	close(open("/tmp/dir/.hidden.c", O_WRONLY | O_CREAT, 0644));
	d = opendir("/tmp/dir");
	n = 0;
	found = 0;
	while ((e = readdir(d)) != NULL) {
		n++;
		if (!strcmp(e->d_name, "c.h")) found = 1;
	}
	expect("opendir and readdir", n == 6 && found);
	rewinddir(d);
	e = readdir(d);
	expect("rewinddir", e && !strcmp(e->d_name, "."));
	expect("closedir", closedir(d) == 0);
	expect("opendir of a file fails", opendir("/tmp/dir/a.c") == NULL && errno == ENOTDIR);
	expect("glob sorts and skips dot files", glob("/tmp/dir/*.c", 0, NULL, &gl) == 0 && gl.gl_pathc == 2 && !strcmp(gl.gl_pathv[0], "/tmp/dir/a.c") && !strcmp(gl.gl_pathv[1], "/tmp/dir/b.c"));
	globfree(&gl);
	chdir("/tmp/dir");
	expect("glob with ? and []", glob("[ab].?", 0, NULL, &gl) == 0 && gl.gl_pathc == 2 && !strcmp(gl.gl_pathv[0], "a.c"));
	globfree(&gl);
	expect("glob without a match", glob("*.zz", 0, NULL, &gl) == GLOB_NOMATCH);
	expect("glob GLOB_NOCHECK", glob("*.zz", GLOB_NOCHECK, NULL, &gl) == 0 && gl.gl_pathc == 1 && !strcmp(gl.gl_pathv[0], "*.zz"));
	globfree(&gl);
	expect("getcwd", getcwd(buf, sizeof(buf)) && !strcmp(buf, "/tmp/dir"));
	chdir("/");

	n = child_status(do_execl);
	expect("execl", WEXITSTATUS(n) == 4);
	n = child_status(do_execlp);
	expect("execlp searches PATH", WEXITSTATUS(n) == 5);
	n = child_status(do_execle);
	expect("execle passes the environment", WEXITSTATUS(n) == 6);
	n = child_status(do_abort);
	expect("abort raises SIGABRT", WIFSIGNALED(n) && WTERMSIG(n) == SIGABRT);

	pw = getpwnam("root");
	expect("getpwnam", pw && pw->pw_uid == 0 && !strcmp(pw->pw_dir, "/root") && !strcmp(pw->pw_shell, "/bin/sh"));
	pw = getpwuid(1000);
	expect("getpwuid", pw && !strcmp(pw->pw_name, "user") && pw->pw_gid == 100);
	expect("getpwnam of a missing user", getpwnam("nobody") == NULL);
	gr = getgrnam("users");
	expect("getgrnam", gr && gr->gr_gid == 100 && gr->gr_mem[0] && !strcmp(gr->gr_mem[0], "user") && !gr->gr_mem[1]);
	gr = getgrgid(10);
	expect("getgrgid", gr && !strcmp(gr->gr_name, "wheel"));
	expect("crypt_t", !strcmp(crypt_t("user", "user"), "$t$ea074035"));

	t = time();
	expect("time and gettimeofday agree", gettimeofday(&tv, NULL) == 0 && tv.tv_sec - t <= 1 && tv.tv_usec >= 0 && tv.tv_usec < 1000000);
	expect("localtime is UTC", localtime(&t)->tm_hour == gmtime(&t)->tm_hour);
	a = time();
	expect("sleep in seconds", sleep(1) == 0 && time() - a <= 2);
	{
		struct timespec x, y;
		clock_gettime(CLOCK_MONOTONIC, &x);
		usleep(50000);
		clock_gettime(CLOCK_MONOTONIC, &y);
		n = (int)((y.tv_sec * 100 + y.tv_nsec / 10000000) - (x.tv_sec * 100 + x.tv_nsec / 10000000));
		expect("usleep 50 ms is 5 ticks", n >= 5 && n <= 7);
	}
	expect("isatty", isatty(0) && !isatty(open("/tmp/lib.txt", O_RDONLY)));
	expect("ttyname", !strcmp(ttyname(0), "/dev/console"));
	p0 = sbrk(0);
	expect("brk grows", brk((char *)p0 + 8192) == 0 && sbrk(0) == (char *)p0 + 8192);
	((char *)p0)[8000] = 1;
	expect("brk shrinks", brk(p0) == 0 && sbrk(0) == p0);
	p1 = malloc(100);
	expect("malloc after brk", p1 != NULL);
	errno = ENOENT;
	fflush(stdout);
	fd = dup(2);
	f = fopen("/tmp/err.txt", "w");
	dup2(fileno(f), 2);
	perror("libtest");
	fclose(f);
	dup2(fd, 2);
	close(fd);
	f = fopen("/tmp/err.txt", "r");
	expect("perror", fgets(buf, sizeof(buf), f) && !strcmp(buf, "libtest: No such file or directory\n"));
	fclose(f);
	printf("%s\n", fails ? "libtest: FAILED" : "libtest: all ok");
	return fails != 0;
}
