#include <stdio.h>
#include <unistd.h>
#include <signal.h>

int main(void) {
	kill(getpid(), SIGTERM);
	printf("SIGTERM ignored\n");
	return 3;
}
