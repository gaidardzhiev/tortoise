#include <stdio.h>
#include <assert.h>
#include "../include/cpu.h"

//test LOAD, ADD and HALT
const uint8_t test_program_simple[] = {
	OP_LOAD, 0x00, 0x34, 0x12,	//LOAD R0, 0x1234
	OP_ADD, 0x00, 0x01, 0x00,	//ADD R0, 0x0001
	OP_HALT				//HALT
};

//test logical ops and CALL/RET
const uint8_t test_program_extended[] = {
	OP_LOAD, 0x00, 0x34, 0x12,	//LOAD R0, 0x1234
	OP_ADD, 0x00, 0x01, 0x00,	//ADD R0, 0x0001
	OP_AND, 0x00, 0x00, 0xFF,	//AND R0, 0xFF00
	OP_OR,  0x00, 0x55, 0x00,	//OR R0, 0x0055
	OP_XOR, 0x00, 0xFF, 0x00,	//XOR R0, 0x00FF
	OP_NOT, 0x00,			//NOT R0
	OP_CALL, 0x1A, 0x00,		// CALL subroutine at 0x001A (26 decimal)
	OP_HALT,			// HALT
					//subroutine at 0x001A:
	OP_LOAD, 0x01, 0x21, 0x43,	// LOAD R1, 0x4321
	OP_RET				// RET
};

int main() {
	//first test LOAD, ADD and HALT
	cpu_init();
	load_program(test_program_simple, sizeof(test_program_simple));
	cpu_run();
	assert(cpu.registers[0] == 0x1235);
	printf("first test passed: R0 = 0x%04X\n\n\n", cpu.registers[0]);
	//second test logical ops and CALL/RET
	cpu_init();
	load_program(test_program_extended, sizeof(test_program_extended));
	cpu_run();
	uint16_t val = 0x1235;
	val &= 0xFF00;
	val |= 0x0055;
	val ^= 0x00FF;
	uint16_t expected_r0 = (uint16_t)(~val);
	assert(cpu.registers[0] == expected_r0);
	assert(cpu.registers[1] == 0x4321);
	printf("second test passed: R0 = 0x%04X, R1 = 0x%04X\n\n\n", cpu.registers[0], cpu.registers[1]);
	return 0;
}
