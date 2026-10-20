#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv) {
	fflush(stdout);
	shutdown(argc > 1 ? atoi(argv[1]) : 0);
	return 1;
}
