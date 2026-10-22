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
#include <plan9.h>
#include <sys/wait.h>

static int fails;
static char got[ERRMAX];
static int count, usr1;
static int dflt;

static void expect(const char *what, int ok) {
	if (ok) printf("ok   %s\n", what);
	else {
		printf("FAIL %s (errno %d)\n", what, errno);
		fails++;
	}
	fflush(stdout);
}

static void handler(void *ureg, char *msg) {
	(void)ureg;
	strncpy(got, msg, ERRMAX - 1);
	count++;
	noted(dflt && strcmp(msg, "keep going") ? NDFLT : NCONT);
}

static void returns(void *ureg, char *msg) {
	(void)ureg;
	(void)msg;
}

static void on_usr1(int sig) {
	(void)sig;
	usr1++;
}

static int note(int pid, const char *file, const char *text) {
	char path[64];
	int fd, r;
	snprintf(path, sizeof(path), "/proc/%d/%s", pid, file);
	fd = open(path, O_WRONLY);
	if (fd < 0) return -1;
	r = write(fd, text, strlen(text));
	close(fd);
	return r;
}

static int child(int setdflt, const char *mode) {
	int pid = fork();
	if (pid == 0) {
		dflt = setdflt;
		notify(handler);
		if (!strcmp(mode, "fault")) {
			volatile int *p = (int *)0;
			return *p;
		}
		for (;;) pause();
	}
	return pid;
}

static int status_of(int pid) {
	int st;
	waitpid(pid, &st, 0);
	return st;
}

int main(void) {
	int pid, st, fd, n;
	char buf[64];
	expect("notify installs a handler", notify(handler) == 0);
	expect("a note to oneself", note(getpid(), "note", "hello note") > 0);
	expect("the handler got it and NCONT went on", count == 1 && !strcmp(got, "hello note"));
	kill(getpid(), SIGUSR2);
	expect("a signal arrives as sys: signal NAME", count == 2 && !strcmp(got, "sys: signal USR2"));
	signal(SIGUSR1, on_usr1);
	kill(getpid(), SIGUSR1);
	expect("a sigaction handler takes precedence", usr1 == 1 && count == 2);
	kill(getpid(), SIGCHLD);
	expect("a signal ignored by default stays ignored", count == 2);
	fd = open("/dev/null", O_RDONLY);
	n = read(fd, buf, 1);
	expect("system calls still work after notes", n == 0);
	close(fd);
	expect("noted outside a handler fails", noted(NCONT) < 0 && errno == EINVAL);
	notify(0);
	signal(SIGUSR2, SIG_IGN);

	pid = child(1, "wait");
	sleep(1);
	note(pid, "note", "interrupt");
	st = status_of(pid);
	expect("NDFLT on interrupt ends the process with INT", WIFSIGNALED(st) && WTERMSIG(st) == SIGINT);
	pid = child(1, "wait");
	sleep(1);
	note(pid, "note", "keep going");
	note(pid, "note", "something else");
	st = status_of(pid);
	expect("NDFLT on any other note is TERM", WIFSIGNALED(st) && WTERMSIG(st) == SIGTERM);
	pid = child(1, "wait");
	sleep(1);
	kill(pid, SIGHUP);
	st = status_of(pid);
	expect("NDFLT on sys: signal HUP is HUP", WIFSIGNALED(st) && WTERMSIG(st) == SIGHUP);
	pid = child(0, "wait");
	sleep(1);
	kill(pid, SIGKILL);
	st = status_of(pid);
	expect("KILL is never a note", WIFSIGNALED(st) && WTERMSIG(st) == SIGKILL);
	pid = child(1, "fault");
	st = status_of(pid);
	expect("a fault reaches the handler and NDFLT gives SEGV", WIFSIGNALED(st) && WTERMSIG(st) == SIGSEGV);
	fflush(stdout);
	pid = fork();
	if (pid == 0) {
		volatile int *p = (int *)0;
		notify(returns);
		*p = 1;
		_exit(0);
	}
	st = status_of(pid);
	expect("a handler that returns without noted dies", WIFSIGNALED(st));
	pid = fork();
	if (pid == 0) {
		setpgid(0, 0);
		if (fork() == 0) {
			for (;;) pause();
		}
		for (;;) pause();
	}
	setpgid(pid, pid);
	sleep(1);
	expect("notepg reaches the group", note(pid, "notepg", "hangup") > 0);
	st = status_of(pid);
	expect("and is HUP for processes without notify", WIFSIGNALED(st) && WTERMSIG(st) == SIGHUP);
	printf("%s\n", fails ? "notetest: FAILED" : "notetest: all ok");
	return fails != 0;
}
