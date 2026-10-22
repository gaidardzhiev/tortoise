/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <termios.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/wait.h>

static int fails;
static struct termios orig;

static void expect(const char *what, int got, int want) {
	if (got == want) printf("ok   %s\n", what);
	else {
		printf("FAIL %s: got %d want %d (errno %d)\n", what, got, want, errno);
		fails++;
	}
	fflush(stdout);
}

static int rd(char *buf, int n) {
	int r = read(0, buf, n);
	buf[r > 0 ? r : 0] = 0;
	printf("\n");
	return r;
}

static unsigned now(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 100 + ts.tv_nsec / 10000000;
}

static void raw(int min, int time, int icrnl) {
	struct termios t = orig;
	t.c_lflag &= ~(ICANON | ECHO | ECHOE);
	if (!icrnl) t.c_iflag &= ~ICRNL;
	t.c_cc[VMIN] = min;
	t.c_cc[VTIME] = time;
	tcsetattr(0, TCSANOW, &t);
}

static int fg_child(int *st) {
	int pid = fork();
	if (pid == 0) {
		char buf[64];
		int r;
		setpgid(0, 0);
		while (tcgetpgrp(0) != getpid()) usleep(1 * 10000);
		r = read(0, buf, sizeof(buf) - 1);
		buf[r > 0 ? r : 0] = 0;
		printf("\nchild read %d: %s", r, buf);
		fflush(stdout);
		_exit(0);
	}
	setpgid(pid, pid);
	tcsetpgrp(0, pid);
	waitpid(pid, st, WUNTRACED);
	return pid;
}

int main(void) {
	char buf[128];
	struct termios t;
	struct winsize ws;
	int r, st, pid;
	unsigned t0;

	signal(SIGTTOU, SIG_IGN);
	tcgetattr(0, &orig);
	expect("canonical, echo and signals are on", (orig.c_lflag & (ICANON | ECHO | ISIG)) == (ICANON | ECHO | ISIG), 1);
	expect("ONLCR is off so output is unchanged", orig.c_oflag & ONLCR, 0);
	expect("TIOCGWINSZ", ioctl(0, TIOCGWINSZ, &ws) == 0 && ws.ws_row == 25 && ws.ws_col == 80, 1);
	expect("the shell's group is in the foreground", tcgetpgrp(0), getpgrp());

	r = rd(buf, sizeof(buf));
	expect("a line", r == 6 && strcmp(buf, "hello\n") == 0, 1);
	r = rd(buf, sizeof(buf));
	expect("DEL erases", strcmp(buf, "abc\n"), 0);
	r = rd(buf, sizeof(buf));
	expect("^H erases", strcmp(buf, "df\n"), 0);
	r = rd(buf, sizeof(buf));
	expect("^U kills the line", strcmp(buf, "ok\n"), 0);
	r = rd(buf, sizeof(buf));
	expect("CR becomes NL", strcmp(buf, "cr\n"), 0);
	r = rd(buf, 3);
	expect("a short read takes part of a line", r == 3 && strcmp(buf, "lon") == 0, 1);
	r = rd(buf, sizeof(buf));
	expect("the rest follows", strcmp(buf, "g\n"), 0);
	expect("^D at the start of a line is end of file", rd(buf, sizeof(buf)), 0);
	r = rd(buf, sizeof(buf));
	expect("^D after text ends the read without a newline", r == 4 && strcmp(buf, "part") == 0, 1);
	t = orig;
	t.c_lflag &= ~ECHO;
	tcsetattr(0, TCSANOW, &t);
	r = rd(buf, sizeof(buf));
	expect("without ECHO nothing is shown", strcmp(buf, "secret\n"), 0);
	tcsetattr(0, TCSANOW, &orig);

	fg_child(&st);
	tcsetpgrp(0, getpgrp());
	expect("^C sends SIGINT to the foreground group", WIFSIGNALED(st) && WTERMSIG(st) == SIGINT, 1);
	fg_child(&st);
	tcsetpgrp(0, getpgrp());
	expect("^\\ sends SIGQUIT", WIFSIGNALED(st) && WTERMSIG(st) == SIGQUIT, 1);
	pid = fg_child(&st);
	expect("^Z sends SIGTSTP and stops it", WIFSTOPPED(st) && WSTOPSIG(st) == SIGTSTP, 1);
	kill(pid, SIGCONT);
	waitpid(pid, &st, 0);
	tcsetpgrp(0, getpgrp());
	expect("after SIGCONT the read starts again", WIFEXITED(st) && WEXITSTATUS(st) == 0, 1);
	t = orig;
	t.c_lflag &= ~ISIG;
	tcsetattr(0, TCSANOW, &t);
	r = rd(buf, sizeof(buf));
	expect("without ISIG ^C is a character", r == 4 && buf[1] == 3, 1);
	tcsetattr(0, TCSANOW, &orig);

	pid = fork();
	if (pid == 0) {
		setpgid(0, 0);
		read(0, buf, 1);
		_exit(0);
	}
	setpgid(pid, pid);
	waitpid(pid, &st, WUNTRACED);
	expect("a background read gets SIGTTIN", WIFSTOPPED(st) && WSTOPSIG(st) == SIGTTIN, 1);
	kill(pid, SIGKILL);
	waitpid(pid, &st, 0);
	pid = fork();
	if (pid == 0) {
		setpgid(0, 0);
		signal(SIGTTIN, SIG_IGN);
		_exit(read(0, buf, 1) == -1 && errno == EIO ? 7 : 1);
	}
	setpgid(pid, pid);
	waitpid(pid, &st, 0);
	expect("with SIGTTIN ignored the read fails with EIO", WEXITSTATUS(st), 7);

	raw(1, 0, 1);
	r = read(0, buf, sizeof(buf));
	expect("raw, VMIN 1: one byte", r == 1 && buf[0] == 'x', 1);
	raw(3, 0, 1);
	r = read(0, buf, sizeof(buf));
	expect("raw, VMIN 3: three bytes", r == 3 && memcmp(buf, "abc", 3) == 0, 1);
	raw(1, 0, 0);
	r = read(0, buf, sizeof(buf));
	expect("raw without ICRNL keeps CR", r == 1 && buf[0] == '\r', 1);
	raw(0, 0, 1);
	r = read(0, buf, 4);
	expect("raw, VMIN 0 VTIME 0: what is there", r == 4 && memcmp(buf, "wxyz", 4) == 0, 1);
	raw(2, 1, 1);
	t0 = now();
	r = read(0, buf, sizeof(buf));
	t0 = now() - t0;
	expect("raw, VMIN 2 VTIME 1: the byte after the gap", r == 1 && buf[0] == 'q', 1);
	expect("the gap was 100 ms", t0 >= 10 && t0 <= 15, 1);
	raw(0, 2, 1);
	t0 = now();
	r = read(0, buf, sizeof(buf));
	t0 = now() - t0;
	expect("raw, VMIN 0 VTIME 2: nothing", r, 0);
	expect("after 200 ms", t0 >= 20 && t0 <= 25, 1);
	tcsetattr(0, TCSANOW, &orig);
	printf("%s\n", fails ? "termtest: FAILED" : "termtest: all ok");
	fflush(stdout);
	poweroff(0);
	return 0;
}
