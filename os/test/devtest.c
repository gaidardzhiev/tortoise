#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
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

static void dev(const char *path, int rdev, int mode) {
	struct stat st;
	char what[80];
	int r = stat(path, &st);
	snprintf(what, sizeof(what), "%s is a character device %d,%d mode %o", path, rdev >> 8, rdev & 0xFF, mode);
	expect(what, r == 0 && S_ISCHR(st.st_mode) && (int)st.st_dev == rdev && (int)(st.st_mode & 07777) == mode, 1);
}

static int all(char *b, int n, int v) {
	int i;
	for (i = 0; i < n; i++)
		if (b[i] != v) return 0;
	return 1;
}

int main(void) {
	char buf[600], buf2[600];
	int fd, r, i, ones;
	struct termios t;
	dev("/dev/console", 0x401, 0666);
	dev("/dev/tty", 0x400, 0666);
	dev("/dev/null", 0x103, 0666);
	dev("/dev/zero", 0x105, 0666);
	dev("/dev/random", 0x108, 0666);
	dev("/dev/urandom", 0x109, 0666);
	dev("/dev/kmsg", 0x10B, 0600);
	dev("/dev/disk0", 0x800, 0600);
	dev("/dev/disk1", 0x801, 0600);
	dev("/dev/fb0", 0x1D00, 0660);
	dev("/dev/kbd", 0xD00, 0660);

	fd = open("/dev/console", O_WRONLY);
	expect("write to /dev/console", write(fd, "console says hello\n", 19), 19);
	close(fd);
	fd = open("/dev/tty", O_RDWR);
	expect("open /dev/tty with a controlling terminal", fd >= 0, 1);
	expect("write to /dev/tty", write(fd, "tty says hello\n", 15), 15);
	expect("/dev/tty is a terminal", isatty(fd), 1);
	close(fd);
	expect("isatty(0)", isatty(0), 1);
	expect("ttyname(0)", strcmp(ttyname(0), "/dev/console"), 0);
	fd = open("/dev/null", O_RDONLY);
	expect("isatty of /dev/null", isatty(fd), 0);
	expect("tcgetattr of /dev/null fails with ENOTTY", tcgetattr(fd, &t) == -1 && errno == ENOTTY, 1);
	close(fd);

	fd = open("/dev/null", O_RDWR);
	expect("/dev/null takes writes", write(fd, buf, 100), 100);
	expect("/dev/null reads end of file", read(fd, buf, 10), 0);
	close(fd);
	fd = open("/dev/zero", O_RDWR);
	memset(buf, 7, 600);
	expect("/dev/zero gives 600 zero bytes", read(fd, buf, 600) == 600 && all(buf, 600, 0), 1);
	expect("/dev/zero takes writes", write(fd, buf, 10), 10);
	close(fd);

	fd = open("/dev/random", O_RDONLY);
	r = read(fd, buf, 512);
	read(fd, buf2, 512);
	close(fd);
	ones = 0;
	for (i = 0; i < 512; i++) {
		int b;
		for (b = 0; b < 8; b++) ones += (buf[i] >> b) & 1;
	}
	expect("/dev/random gives 512 bytes", r, 512);
	expect("about half of the bits are set", ones > 1800 && ones < 2300, 1);
	expect("two reads differ", memcmp(buf, buf2, 512) != 0, 1);
	fd = open("/dev/urandom", O_RDONLY);
	expect("/dev/urandom gives 16 bytes", read(fd, buf, 16), 16);
	close(fd);

	fd = open("/dev/kmsg", O_RDONLY);
	r = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	buf[r > 0 ? r : 0] = 0;
	expect("/dev/kmsg holds the boot message", strstr(buf, "TORTOISE OS booting") != NULL, 1);

	fd = open("/dev/disk0", O_RDONLY);
	expect("seek to the file system on disk0", lseek(fd, 1024 * 512, SEEK_SET), 1024 * 512);
	r = read(fd, buf, 512);
	expect("disk0 superblock has the TFS2 magic", r == 512 && memcmp(buf, "TFS2", 4) == 0, 1);
	expect("disk0 size", lseek(fd, 0, SEEK_END), 65536 * 512);
	close(fd);
	fd = open("/dev/disk1", O_RDONLY);
	expect("disk1 without a second disk is empty", read(fd, buf, 512), 0);
	close(fd);

	fd = open("/dev/fb0", O_RDWR);
	expect("fb0 takes pixels", write(fd, "\377\377\377\377", 4), 4);
	lseek(fd, 0, SEEK_SET);
	memset(buf, 9, 16);
	expect("fb0 reads zeros without SDL", read(fd, buf, 16) == 16 && all(buf, 16, 0), 1);
	close(fd);
	fd = open("/dev/kbd", O_RDONLY | O_NONBLOCK);
	expect("kbd has no characters without SDL", read(fd, buf, 16), 0);
	close(fd);
	printf("%s\n", fails ? "devtest: FAILED" : "devtest: all ok");
	return fails != 0;
}
