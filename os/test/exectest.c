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
#include <sys/wait.h>

static void run(const char *path, char **argv, char **envp) {
	int pid, st;
	fflush(stdout);
	pid = fork();
	if (pid == 0) {
		execve(path, argv, envp);
		printf("execve %s failed, errno %d\n", path, errno);
		_exit(127);
	}
	waitpid(pid, &st, 0);
	printf("exit %d\n", WEXITSTATUS(st));
}

int main(void) {
	char *argv[4], *envp[4], *noargs[1];
	int fd;
	argv[0] = "showargs";
	argv[1] = "one";
	argv[2] = "two words";
	argv[3] = NULL;
	envp[0] = "HOME=/root";
	envp[1] = "PATH=/bin:/test";
	envp[2] = "EMPTY=";
	envp[3] = NULL;
	noargs[0] = NULL;
	fd = open("/etc/motd", O_RDONLY);
	dup2(fd, 5);
	dup2(fd, 6);
	close(fd);
	fcntl(6, F_SETFD, FD_CLOEXEC);
	printf("execve with an environment, fd 6 close on exec:\n");
	run("/test/showargs", argv, envp);
	printf("exec (syscall 8) passes no environment:\n");
	fflush(stdout);
	if (fork() == 0) {
		exec("/test/showargs", argv);
		_exit(127);
	}
	wait(NULL);
	printf("script with an interpreter argument:\n");
	run("/test/script1", argv, envp);
	printf("script without an argument, empty argv:\n");
	run("/test/script2", noargs, NULL);
	printf("relative script path:\n");
	chdir("/test");
	run("script2", argv, NULL);
	printf("missing interpreter:\n");
	run("/test/script3", argv, NULL);
	printf("not executable:\n");
	run("/etc/motd", argv, NULL);
	printf("ignored signals stay ignored across exec:\n");
	signal(SIGTERM, SIG_IGN);
	fflush(stdout);
	if (fork() == 0) {
		char *a[3];
		a[0] = "sh";
		a[1] = NULL;
		execve("/test/selfkill", a, NULL);
		_exit(127);
	}
	{
		int st;
		wait(&st);
		printf("exit %d\n", WEXITSTATUS(st));
	}
	setenv("ADDED", "yes", 1);
	setenv("HOME", "/home", 0);
	printf("getenv after setenv: %s\n", getenv("ADDED"));
	return 0;
}
