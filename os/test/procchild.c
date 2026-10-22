/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/mman.h>

int main(int argc, char **argv) {
	sigset_t set;
	int fd, p[2];
	char *a, *b;
	nice(3);
	signal(SIGUSR1, SIG_IGN);
	signal(SIGINT, SIG_IGN);
	sigemptyset(&set);
	sigaddset(&set, SIGUSR2);
	sigprocmask(SIG_BLOCK, &set, NULL);
	raise(SIGUSR2);
	chdir("/tmp");
	fd = open("/etc/motd", O_RDONLY);
	dup2(fd, 7);
	close(fd);
	pipe(p);
	a = mmap(NULL, 8192, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	b = mmap(NULL, 4096, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	a[0] = b[0];
	write(1, "r", 1);
	for (;;) pause();
}
