#include <stdio.h>
#include <stdlib.h>
#include "cpu.h"

void load_binary(const char *filename) {
	FILE *f = fopen(filename, "rb");
	if (!f) {
		perror("loading binary");
		exit(1);
	}
	size_t read = fread(cpu.memory, 1, MEMORY_SIZE, f);
	for (size_t i = read; i < MEMORY_SIZE; i++) {
		cpu.memory[i] = 0xFF;
	}
	printf("loaded %zu bytes into memory\n", read);
	fclose(f);
}

int main(int argc, char *argv[]) {
	if (argc < 2) {
		printf("usage: %s <program.bin>\n", argv[0]);
		return 1;
	}
	cpu_init();
	load_binary(argv[1]);
	cpu_run();
	return 0;
}
