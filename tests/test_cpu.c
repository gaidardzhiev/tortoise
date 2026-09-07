#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "cpu.h"

const uint8_t simple[] = {
	OP_LOAD, 0x00, 0x34, 0x12,
	OP_ADD, 0x00, 0x01, 0x00,
	OP_HALT
};

const uint8_t extended[] = {
	OP_LOAD, 0x00, 0x34, 0x12,
	OP_ADD, 0x00, 0x01, 0x00,
	OP_AND, 0x00, 0x00, 0xFF,
	OP_OR,  0x00, 0x55, 0x00,
	OP_XOR, 0x00, 0xFF, 0x00,
	OP_NOT, 0x00,
	OP_CALL, 0x1A, 0x00,
	OP_HALT,
	OP_LOAD, 0x01, 0x21, 0x43,
	OP_RET
};

const uint8_t shift_prog[] = {
	OP_LOAD, 0x00, 0x01, 0x00,
	OP_SHL, 0x00, 0x04, 0x00,
	OP_SHR, 0x00, 0x02, 0x00,
	OP_HALT
};

const uint8_t mul_div_prog[] = {
	OP_LOAD, 0x00, 0x0A, 0x00,
	OP_MUL, 0x00, 0x06, 0x00,
	OP_DIV, 0x00, 0x04, 0x00,
	OP_HALT
};

const uint8_t int_prog[] = {
	OP_LOAD, 0x00, 0x0C, 0x00,
	OP_STORE, 0x00, 0x04, 0x01,
	OP_STI,
	OP_INT, 0x02,
	OP_HALT,
	OP_LOAD, 0x01, 0x42, 0x00,
	OP_IRET
};

const uint8_t reg32_prog[] = {
	OP_LOAD32, 0x00, 0x78, 0x56, 0x34, 0x12,
	OP_ADD32, 0x00, 0x02, 0x00, 0x00, 0x00,
	OP_STORE32, 0x00, 0x00, 0x00, 0x02, 0x00,
	OP_SUB32, 0x00, 0x70, 0x56, 0x34, 0x12,
	OP_HALT
};

const uint8_t io_prog[] = {
	OP_IN, 0x00, 0x01, 0xFF,
	OP_IN, 0x01, 0x02, 0xFF,
	OP_IN, 0x02, 0x03, 0xFF,
	OP_HALT
};

const uint8_t advanced_prog[] = {
	OP_LOAD, 0x00, 0x0A, 0x00,
	OP_MOV, 0x10,
	OP_ADD_REG, 0x10,
	OP_SUB_REG, 0x10,
	OP_PUSH, 0x01,
	OP_POP, 0x02,
	OP_LOAD, 0x03, 0x00, 0x20,
	OP_STORE_INDIR, 0x31, 0x04, 0x00,
	OP_LOAD_INDIR, 0x43, 0x04, 0x00,
	OP_HALT
};

int main(void) {
	cpu_init();
	load_program(simple, sizeof(simple));
	cpu_run();
	assert(cpu.registers[0] == 0x1235);
	printf("first test passed: R0 = 0x%04X\n\n\n", cpu.registers[0]);

	cpu_init();
	load_program(extended, sizeof(extended));
	cpu_run();
	uint16_t val = 0x1235;
	val &= 0xFF00;
	val |= 0x0055;
	val ^= 0x00FF;
	uint16_t expected_r0 = (uint16_t)(~val);
	assert(cpu.registers[0] == expected_r0);
	assert(cpu.registers[1] == 0x4321);
	printf("second test passed: R0 = 0x%04X, R1 = 0x%04X\n\n\n", cpu.registers[0], cpu.registers[1]);

	cpu_init();
	load_program(shift_prog, sizeof(shift_prog));
	cpu_run();
	assert(cpu.registers[0] == 4);
	printf("shift test passed: R0 = 0x%04X\n\n\n", cpu.registers[0]);

	cpu_init();
	load_program(mul_div_prog, sizeof(mul_div_prog));
	cpu_run();
	assert(cpu.registers[0] == 15);
	printf("mul_div test passed: R0 = %u\n\n\n", cpu.registers[0]);

	cpu_init();
	load_program(int_prog, sizeof(int_prog));
	cpu_run();
	assert(cpu.registers[1] == 0x42);
	assert(cpu.int_enabled == 1);
	printf("interrupt test passed: R1 = 0x%04X\n\n\n", cpu.registers[1]);

	cpu_init();
	load_program(reg32_prog, sizeof(reg32_prog));
	cpu_run();
	assert(cpu.registers[0] == 10);
	uint32_t mem_val = cpu.memory[0x20000] | (cpu.memory[0x20001] << 8) | (cpu.memory[0x20002] << 16) | (cpu.memory[0x20003] << 24);
	assert(mem_val == 0x1234567A);
	printf("reg32 test passed: R0 = %u, mem[0x20000] = 0x%08X\n\n\n", cpu.registers[0], mem_val);

	cpu_init();
	load_program(io_prog, sizeof(io_prog));
	cpu_run();
	assert(cpu.registers[0] == 0x03);
	assert(cpu.registers[1] >= 1);
	printf("io test passed: R0 = 0x%X, R1 = %u, R2 = 0x%X\n\n\n", cpu.registers[0], cpu.registers[1], cpu.registers[2]);

	cpu_init();
	load_program(advanced_prog, sizeof(advanced_prog));
	cpu_run();
	assert(cpu.registers[1] == 10);
	assert(cpu.registers[2] == 10);
	assert(cpu.registers[4] == 10);
	printf("advanced ops test passed: R1=%u, R2=%u, R4=%u\n\n\n", cpu.registers[1], cpu.registers[2], cpu.registers[4]);

	char dis_buf[64];
	cpu_disassemble(0, dis_buf, sizeof(dis_buf));
	assert(strlen(dis_buf) > 0);
	printf("disassembly test passed: %s\n\n\n", dis_buf);

	return 0;
}
