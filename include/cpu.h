#ifndef CPU_H
#define CPU_H

#include <stdint.h>

#define MEMORY_SIZE 65536//64KB memory

typedef struct {
	uint16_t PC;// Program Counter
	uint16_t registers[8];// General purpose registers R0-R7
	uint8_t memory[MEMORY_SIZE];// RAM
	uint8_t halted;// Halt flag
} CPU;

//opcodes
typedef enum {
	OP_NOP = 0x00,
	OP_LOAD = 0x01,
	OP_ADD = 0x02,
	OP_STORE = 0x03,
	OP_JMP = 0x04,
	OP_JZ = 0x05,
	OP_HALT = 0xFF,
	OP_AND = 0x06,
	OP_OR = 0x07,
	OP_XOR = 0x08,
	OP_NOT = 0x09,
	OP_CALL = 0x0A,
	OP_RET = 0x0B,
} Opcode;

//global CPU state
extern CPU cpu;

//initializes CPU state
void cpu_init(void);

//run the CPU fetch decode execute loop
void cpu_run(void);

//load a program binary into memory starting at address 0
void load_program(const uint8_t *program, size_t size);

#endif // CPU_H
