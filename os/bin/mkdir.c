#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv) {
	int i, rc = 0;
	for (i = 1; i < argc; i++)
		if (mkdir(argv[i]) < 0) {
			printf("mkdir: cannot create %s\n", argv[i]);
			rc = 1;
		}
	return rc;
}
