#include <stdio.h>
#include <unistd.h>
#include <errno.h>

int main(int argc, char **argv) {
	if (argc != 2) {
		printf("usage: umount target\n");
		return 1;
	}
	if (umount(argv[1]) < 0) {
		printf("umount: cannot unmount %s: error %d\n", argv[1], errno);
		return 1;
	}
	return 0;
}
