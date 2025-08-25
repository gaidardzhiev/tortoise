#include <stdio.h>
#include <string.h>
#include "cpu.h"

typedef enum {
	OP_NOP = 0x00,
	OP_LOAD = 0x01,// LOAD reg, immediate
	OP_ADD = 0x02,// ADD reg, immediate
	OP_STORE = 0x03,// STORE reg, address
	OP_JMP = 0x04,// JMP address
	OP_JZ = 0x05,// JZ reg, address
	OP_HALT = 0xFF// HALT execution
} Opcode;

static CPU cpu;

void cpu_init(void) {
	memset(&cpu, 0, sizeof(cpu));
	cpu.PC = 0;
	cpu.halted = 0;
}

void load_program(const uint8_t *program, size_t size) {
	if (size > MEMORY_SIZE) {
		fprintf(stderr, "program too large to load...\n");
		return;
	}
	memcpy(cpu.memory, program, size);
}

static uint8_t fetch_byte(void) {
	return cpu.memory[cpu.PC++];
}

static uint16_t fetch_word(void) {
	uint16_t low = fetch_byte();
	uint16_t high = fetch_byte();
	return (high << 8) | low;
}

static void execute_instruction(void) {
	uint8_t opcode = fetch_byte();
	switch(opcode) {
	case OP_NOP:
		//no operation
		break;
	case OP_LOAD: {
		uint8_t reg = fetch_byte();
		uint16_t val = fetch_word();
		if (reg < 8) {
			cpu.registers[reg] = val;
		}
		break;
	}
	case OP_ADD: {
		uint8_t reg = fetch_byte();
		uint16_t val = fetch_word();
		if (reg < 8) {
			cpu.registers[reg] += val;
		}
		break;
	}
	case OP_STORE: {
		uint8_t reg = fetch_byte();
		uint16_t addr = fetch_word();
		if (reg < 8 && addr < MEMORY_SIZE) {
			uint16_t val = cpu.registers[reg];
			cpu.memory[addr] = val & 0xFF;
			cpu.memory[addr + 1] = (val >> 8) & 0xFF;
		}
		break;
	}
	case OP_JMP: {
		uint16_t addr = fetch_word();
		if (addr < MEMORY_SIZE) {
			cpu.PC = addr;
		}
		break;
	}
	case OP_JZ: {
		uint8_t reg = fetch_byte();
		uint16_t addr = fetch_word();
		if (reg < 8 && cpu.registers[reg] == 0 && addr < MEMORY_SIZE) {
			cpu.PC = addr;
		}
		break;
	}
	case OP_HALT:
		cpu.halted = 1;
		break;
	default:
		printf("unknown opcode 0x%02X at %04X\n", opcode, cpu.PC - 1);
		cpu.halted = 1;
	}
}

void cpu_run(void) {
	while (!cpu.halted) {
		execute_instruction();
	}
}
