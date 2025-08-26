#ifndef CPU_H
#define CPU_H

#include <stdint.h>

#define MEMORY_SIZE 65536		//64KB memory

typedef struct {
	uint32_t PC;			//program counter
	uint32_t registers[8];		//general purpose registers R0-R7
	uint32_t SP;			//stack Pointer
	uint8_t memory[MEMORY_SIZE];	//RAM
	uint8_t halted;			//halt flag
} CPU;

//ppcode definitions including logical ops and calls
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
	OP_HALT = 0xFF
} Opcode;

//external CPU state variable
extern CPU cpu;

//initializes CPU state: registers, PC, SP, memory
void cpu_init(void);

//main loop: fetch, decode, execute instructions until halted
void cpu_run(void);

//load machine code program into CPU memory
void load_program(const uint8_t *program, size_t size);

#endif // CPU_H
