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
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

#define TIOCGPTN 0x80045430u

static int fails;

static void expect(const char *what, int ok) {
	if (ok) printf("ok   %s\n", what);
	else {
		printf("FAIL %s (errno %d)\n", what, errno);
		fails++;
	}
	fflush(stdout);
}

int main(void) {
	int m, s, n = -1, m2, n2 = -1, r, found = 0;
	char path[32], buf[128];
	struct stat st;
	struct termios t;
	DIR *d;
	struct dirent *e;
	m = open("/dev/ptmx", O_RDWR);
	expect("open /dev/ptmx", m >= 0);
	expect("TIOCGPTN gives the number", ioctl(m, TIOCGPTN, &n) == 0 && n == 0);
	snprintf(path, sizeof(path), "/dev/pts/%d", n);
	s = open(path, O_RDWR);
	expect("open the slave", s >= 0);
	expect("the slave is a terminal", isatty(s) && !strcmp(ttyname(s), "/dev/pts/0"));
	expect("the slave is character device 136,0", stat(path, &st) == 0 && S_ISCHR(st.st_mode) && st.st_dev == (136 << 8));
	expect("/dev/ptmx is 5,2", stat("/dev/ptmx", &st) == 0 && st.st_dev == ((5 << 8) | 2));
	m2 = open("/dev/ptmx", O_RDWR);
	expect("a second pair is number 1", m2 >= 0 && ioctl(m2, TIOCGPTN, &n2) == 0 && n2 == 1);
	d = opendir("/dev/pts");
	while (d && (e = readdir(d)) != NULL)
		if (!strcmp(e->d_name, "0") || !strcmp(e->d_name, "1")) found++;
	if (d) closedir(d);
	expect("/dev/pts lists both", found == 2);
	close(m2);
	expect("writing to the master is input for the slave", write(m, "line one\n", 9) == 9 && read(s, buf, sizeof(buf)) == 9 && !memcmp(buf, "line one\n", 9));
	r = read(m, buf, sizeof(buf));
	expect("the slave echoes to the master", r == 9 && !memcmp(buf, "line one\n", 9));
	expect("writing to the slave is output for the master", write(s, "output", 6) == 6 && read(m, buf, sizeof(buf)) == 6 && !memcmp(buf, "output", 6));
	tcgetattr(s, &t);
	t.c_lflag &= ~(ICANON | ECHO);
	tcsetattr(s, TCSANOW, &t);
	expect("raw mode: bytes pass at once without echo", write(m, "xy", 2) == 2 && read(s, buf, sizeof(buf)) == 2);
	fcntl(m, F_SETFL, O_NONBLOCK);
	expect("nothing was echoed", read(m, buf, sizeof(buf)) == -1 && errno == EAGAIN);
	close(m);
	expect("after the master closes the slave reads end of file", read(s, buf, sizeof(buf)) == 0);
	expect("and writes fail with EIO", write(s, "x", 1) == -1 && errno == EIO);
	close(s);
	expect("the pair is gone", open(path, O_RDWR) == -1);
	printf("%s\n", fails ? "ptytest: FAILED" : "ptytest: all ok");
	return fails != 0;
}
