#ifndef CPU_H
#define CPU_H

#include <stdint.h>
#include <stddef.h>

#define MEMORY_SIZE 1048576

#define FLAG_Z (1 << 0)
#define FLAG_C (1 << 1)
#define FLAG_S (1 << 2)
#define FLAG_V (1 << 3)
#define FLAG_I (1 << 4)
#define FLAG_U (1 << 5)

#define IVT_BASE 0x0100

#define IO_CONSOLE_DATA 0xFF00
#define IO_CONSOLE_STATUS 0xFF01
#define IO_TIMER 0xFF02
#define IO_RNG 0xFF03
#define IO_SYS 0xFF04
#define IO_DISK_SECTOR 0xFF10
#define IO_DISK_CMD 0xFF11
#define IO_DISK_STATUS 0xFF12
#define IO_DISK_BUFFER 0xFF13

#define MODE_SUPERVISOR 0
#define MODE_USER 1
#define FAULT_GP 13

#define VIDEO_BASE 0xB8000
#define VIDEO_SIZE 2000

#define DISK_SECTOR_SIZE 512
#define DISK_NUM_SECTORS 256

typedef struct {
	uint32_t PC;
	uint32_t registers[8];
	uint32_t SP;
	uint8_t memory[MEMORY_SIZE];
	uint8_t halted;
	uint8_t flags;
	uint8_t int_enabled;
	uint8_t int_pending;
	uint8_t int_vector;
	uint8_t mode;
	uint32_t syscall_pc;
	uint32_t disk_sector;
	uint32_t disk_buffer;
	uint8_t disk[DISK_SECTOR_SIZE * DISK_NUM_SECTORS];
} CPU;

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
	OP_SHL = 0x14,
	OP_SHR = 0x15,
	OP_MUL = 0x16,
	OP_DIV = 0x17,
	OP_INT = 0x18,
	OP_IRET = 0x19,
	OP_CLI = 0x1A,
	OP_STI = 0x1B,
	OP_LOAD32 = 0x20,
	OP_STORE32 = 0x21,
	OP_ADD32 = 0x22,
	OP_SUB32 = 0x23,
	OP_MUL32 = 0x24,
	OP_DIV32 = 0x25,
	OP_SHL32 = 0x26,
	OP_SHR32 = 0x27,
	OP_AND32 = 0x28,
	OP_OR32 = 0x29,
	OP_XOR32 = 0x2A,
	OP_CMP32 = 0x2B,
	OP_MOV = 0x30,
	OP_ADD_REG = 0x31,
	OP_SUB_REG = 0x32,
	OP_CMP_REG = 0x33,
	OP_AND_REG = 0x34,
	OP_OR_REG = 0x35,
	OP_XOR_REG = 0x36,
	OP_MUL_REG = 0x37,
	OP_DIV_REG = 0x38,
	OP_PUSH = 0x39,
	OP_POP = 0x3A,
	OP_LOAD_INDIR = 0x3B,
	OP_STORE_INDIR = 0x3C,
	OP_LOAD32_INDIR = 0x3D,
	OP_STORE32_INDIR = 0x3E,
	OP_SYSCALL = 0x3F,
	OP_SYSRET = 0x40,
	OP_HALT = 0xFF
} Opcode;

extern CPU cpu;
void cpu_init(void);
void cpu_run(void);
void load_program(const uint8_t *program, size_t size);
void cpu_interrupt(uint8_t vector);
int cpu_disassemble(uint32_t addr, char *out, size_t out_size);
void cpu_debug(void);

#endif
