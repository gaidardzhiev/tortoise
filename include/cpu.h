/*
 * Copyright (C) 2025 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#ifndef CPU_H
#define CPU_H

#include <stdint.h>

#define MEMORY_SIZE 65536

typedef struct {
	uint32_t PC;
	uint32_t registers[8];
	uint32_t SP;
	uint8_t memory[MEMORY_SIZE];
	uint8_t halted;
	uint8_t flags;
} CPU;

#define FLAG_Z (1 << 0)
#define FLAG_C (1 << 1)
#define FLAG_S (1 << 2)
#define FLAG_V (1 << 3)

typedef enum {
	OP_NOP = 0x00,
	OP_LOAD = 0x01,
	OP_ADD = 0x02,
	OP_STORE = 0x03,
	OP_JMP = 0x04,
	OP_JZ = 0x05,
	OP_AND = 0x06,
	OP_OR = 0x07,
	OP_XOR = 0x08,
	OP_NOT = 0x09,
	OP_CALL = 0x0A,
	OP_RET = 0x0B,
	OP_IN = 0x0C,
	OP_OUT = 0x0D,
	OP_SUB = 0x0E,
	OP_CMP = 0x0F,
	OP_JC = 0x10,
	OP_JN = 0x11,
	OP_JO = 0x12,
	OP_JNZ = 0x13,
	OP_HALT = 0xFF
} Opcode;

extern CPU cpu;
void cpu_init(void);
void cpu_run(void);
void load_program(const uint8_t *program, size_t size);

#endif
