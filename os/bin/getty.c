/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <plan9.h>
#include <sys/ioctl.h>
#include <pwd.h>
#include <grp.h>

int main(int argc, char **argv) {
	const char *dev = argc > 1 ? argv[1] : "/dev/console";
	char name[32], *args[3];
	struct passwd *pw;
	int fd, n;
	setsid();
	fd = open(dev, O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "getty: cannot open %s\n", dev);
		usleep(100 * 10000);
		return 1;
	}
	ioctl(fd, TIOCSCTTY, 0);
	dup2(fd, 0);
	dup2(fd, 1);
	dup2(fd, 2);
	if (fd > 2) close(fd);
	fd = open("/etc/autologin", O_RDONLY);
	if (fd >= 0) {
		n = read(fd, name, sizeof(name) - 1);
		close(fd);
		if (n < 0) n = 0;
		name[n] = 0;
		while (n > 0 && (name[n - 1] == '\n' || name[n - 1] == ' ')) name[--n] = 0;
		pw = getpwnam(name);
		if (pw) {
			if (access("/lib/namespace", R_OK) == 0 && (rfork(RFNAMEG) < 0 || addns(pw->pw_name, "/lib/namespace") < 0)) fprintf(stderr, "getty: cannot build the namespace\n");
			initgroups(pw->pw_name, pw->pw_gid);
			setgid(pw->pw_gid);
			setuid(pw->pw_uid);
		}
		setenv("USER", name, 1);
		setenv("LOGNAME", name, 1);
		setenv("HOME", pw ? pw->pw_dir : "/", 1);
		setenv("SHELL", pw && pw->pw_shell[0] ? pw->pw_shell : "/bin/sh", 1);
		setenv("PATH", "/bin", 1);
		args[0] = "sh";
		args[1] = NULL;
		execv("/bin/sh", args);
		fprintf(stderr, "getty: cannot run /bin/sh\n");
	} else {
		args[0] = "login";
		args[1] = NULL;
		execv("/bin/login", args);
		fprintf(stderr, "getty: cannot run /bin/login\n");
	}
	usleep(100 * 10000);
	return 1;
}
