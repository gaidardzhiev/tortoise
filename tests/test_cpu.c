#include <stdio.h>
#include <assert.h>
#include "../include/cpu.h"

//test LOAD, ADD and HALT
const uint8_t test_program[] = {
	OP_LOAD, 0x00, 0x34, 0x12,// LOAD R0, 0x1234
	OP_ADD, 0x00, 0x01, 0x00,// ADD R0, 0x0001
	OP_HALT// HALT
};

int main() {
	cpu_init();
	load_program(test_program, sizeof(test_program));
	cpu_run();
	//after execution, R0 should be 0x1235
	assert(cpu.registers[0] == 0x1235);
	printf("test passed: R0 = 0x%04X\n", cpu.registers[0]);
	return 0;
}
