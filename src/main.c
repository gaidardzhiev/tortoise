#include <stdio.h>
#include "cpu.h"

int main() {
	printf("starting TORTOISE CPU emulator\n");
	cpu_init();
	cpu_run();
	printf("emulator stopped...\n");
	return 0;
}
