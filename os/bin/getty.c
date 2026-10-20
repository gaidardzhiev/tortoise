#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/ioctl.h>

int main(int argc, char **argv) {
	const char *dev = argc > 1 ? argv[1] : "/dev/console";
	char name[32], *args[3];
	int fd, n;
	setsid();
	fd = open(dev, O_RDWR);
	if (fd < 0) {
		printf("getty: cannot open %s\n", dev);
		sleep(100);
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
		setenv("USER", name, 1);
		setenv("LOGNAME", name, 1);
		setenv("HOME", "/", 1);
		setenv("SHELL", "/bin/sh", 1);
		setenv("PATH", "/bin", 1);
		args[0] = "sh";
		args[1] = NULL;
		execv("/bin/sh", args);
		printf("getty: cannot run /bin/sh\n");
	} else {
		args[0] = "login";
		args[1] = NULL;
		execv("/bin/login", args);
		printf("getty: cannot run /bin/login\n");
	}
	sleep(100);
	return 1;
}
