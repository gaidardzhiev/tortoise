/*
 * Copyright (C) 2025 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <string.h>
#include "cpu.h"

CPU cpu;

static uint8_t fetch_byte(void) {
	return cpu.memory[cpu.PC++];
}

static uint16_t fetch_word(void) {
	uint16_t low = fetch_byte();
	uint16_t high = fetch_byte();
	return (high << 8) | low;
}

void cpu_init(void) {
	memset(&cpu, 0, sizeof(cpu));
	cpu.PC = 0;
	cpu.SP = MEMORY_SIZE;
	cpu.halted = 0;
	printf("cpu_init: PC=0x%X SP=0x%X\n", cpu.PC, cpu.SP);
}

void load_program(const uint8_t *program, size_t size) {
	if (size > MEMORY_SIZE) {
		fprintf(stderr, "program too large\n");
		return;
	}
	memcpy(cpu.memory, program, size);
}

static void push_word(uint16_t val) {
	if (cpu.SP < 2) {
		fprintf(stderr, "stack overflow on push!\n");
		cpu.halted = 1;
		return;
	}
	cpu.SP -= 2;
	cpu.memory[cpu.SP] = val & 0xFF;
	cpu.memory[cpu.SP + 1] = (val >> 8) & 0xFF;
	printf("push_word: SP=0x%X val=0x%X\n", cpu.SP, val);
}

static uint16_t pop_word(void) {
	if (cpu.SP > MEMORY_SIZE - 2) {
		fprintf(stderr, "stack underflow on pop!\n");
		cpu.halted = 1;
		return 0;
	}
	uint16_t low = cpu.memory[cpu.SP];
	uint16_t high = cpu.memory[cpu.SP + 1];
	uint16_t val = (high << 8) | low;
	cpu.SP += 2;
	printf("pop_word: SP=0x%X val=0x%X\n", cpu.SP, val);
	return val;
}

static void execute_instruction(void) {
	if (cpu.PC >= MEMORY_SIZE) {
		fprintf(stderr, "PC out of bounds\n");
		cpu.halted = 1;
		return;
	}
	uint8_t opcode = fetch_byte();
	printf("executing opcode 0x%X at PC=0x%X\n", opcode, cpu.PC - 1);
	uint8_t reg;
	uint32_t val, addr;
	switch (opcode) {
	case OP_NOP:
		break;
	case OP_LOAD:
		reg = fetch_byte();
		val = fetch_word();
		if (reg < 8) cpu.registers[reg] = val;
		break;
	case OP_ADD:
		reg = fetch_byte();
		val = fetch_word();
		if (reg < 8) cpu.registers[reg] += val;
		break;
	case OP_STORE:
		reg = fetch_byte();
		addr = fetch_word();
		if (reg < 8 && addr + 1 < MEMORY_SIZE) {
			val = cpu.registers[reg];
			cpu.memory[addr] = val & 0xFF;
			cpu.memory[addr + 1] = (val >> 8) & 0xFF;
		}
		break;
	case OP_JMP:
		addr = fetch_word();
		if (addr < MEMORY_SIZE) cpu.PC = addr;
		break;
	case OP_JZ:
		reg = fetch_byte();
		addr = fetch_word();
		if (reg < 8 && cpu.registers[reg] == 0 && addr < MEMORY_SIZE) cpu.PC = addr;
		break;
	case OP_AND:
		reg = fetch_byte();
		val = fetch_word();
		if (reg < 8) {
			printf("AND before: R%d=0x%X val=0x%X\n", reg, cpu.registers[reg], val);
			cpu.registers[reg] &= val;
			printf("AND after: R%d=0x%X\n", reg, cpu.registers[reg]);
		}
		break;
	case OP_OR:
		reg = fetch_byte();
		val = fetch_word();
		if (reg < 8) {
			printf("OR before: R%d=0x%X val=0x%X\n", reg, cpu.registers[reg], val);
			cpu.registers[reg] |= val;
			printf("OR after: R%d=0x%X\n", reg, cpu.registers[reg]);
		}
		break;
	case OP_XOR:
		reg = fetch_byte();
		val = fetch_word();
		if (reg < 8) {
			printf("XOR before: R%d=0x%X val=0x%X\n", reg, cpu.registers[reg], val);
			cpu.registers[reg] ^= val;
			printf("XOR after: R%d=0x%X\n", reg, cpu.registers[reg]);
		}
		break;
	case OP_NOT:
		reg = fetch_byte();
		if (reg < 8) {
			printf("NOT before: R%d=0x%X\n", reg, cpu.registers[reg]);
			//cpu.registers[reg] = ~cpu.registers[reg];
			cpu.registers[reg] = (uint16_t)(~cpu.registers[reg]);
			printf("NOT after: R%d=0x%X\n", reg, cpu.registers[reg]);
		}
		break;
	case OP_CALL:
		addr = fetch_word();
		push_word(cpu.PC);
		printf("CALL to 0x%X from 0x%X\n", addr, cpu.PC);
		cpu.PC = addr;
		break;
	case OP_RET:
		addr = pop_word();
		printf("RET to 0x%X\n", addr);
		cpu.PC = addr;
		break;
	case OP_IN:
		reg = fetch_byte();
		addr = fetch_word();
		if (reg < 8 && addr == 0xFF00)
			cpu.registers[reg] = 'A'; //example input
		break;
	case OP_OUT:
		reg = fetch_byte();
		addr = fetch_word();
		if (reg < 8 && addr == 0xFF00) {
			putchar(cpu.registers[reg] & 0xFF);
			fflush(stdout);
		}
		break;
	case OP_HALT:
		cpu.halted = 1;
		break;
	default:
		printf("unknown opcode 0x%X at PC=0x%X\n", opcode, cpu.PC-1);
		cpu.halted = 1;
		break;
	}
}

void cpu_run(void) {
	while (!cpu.halted) {
		execute_instruction();
	}
}
