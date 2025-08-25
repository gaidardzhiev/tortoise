#ifndef CPU_H
#define CPU_H

#include <stdint.h>

#define MEMORY_SIZE 65536//64KB memory

typedef struct {
	uint16_t PC;// program counter
	uint16_t registers[8]; // general purpose registers R0-R7
	uint8_t memory[MEMORY_SIZE];// RAM
	uint8_t halted;// halt flag
} CPU;

//initializes CPU state
void cpu_init(void);

//run the CPU fetch decode execute loop
void cpu_run(void);

//load a program binary into memory starting at address 0
void load_program(const uint8_t *program, size_t size);

#endif // CPU_H
