/*
 * Copyright (C) 2025 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#ifndef CPU_H
#define CPU_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

#define MEMORY_SIZE 1048576
#define MAX_RAM_MB 256

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

#define ROM_BASE 0xFFFF0000u
#define ROM_SIZE 0x10000u
#define DEV_BASE 0xF0000000u
#define DEV_END 0xF0010000u

#define CR0_EXT 1
#define CR0_PROT 2
#define CR0_PG 4

#define CR_CTRL 0
#define CR_EVT 1
#define CR_FADDR 2
#define CR_PTBASE 3
#define CR_KSP 4
#define CR_BASE 5
#define CR_LIMIT 6
#define CR_CAUSE 7
#define CR_RAMSIZE 8
#define NUM_CR 9

#define VEC_DIV 0
#define VEC_BRK 3
#define VEC_UD 4
#define VEC_BUS 5
#define VEC_DF 6
#define VEC_GP 13
#define VEC_PF 14
#define VEC_IRQ 32
#define VEC_SYSCALL 48

#define PTE_P 1
#define PTE_W 2
#define PTE_U 4
#define PTE_X 8
#define PTE_A 16
#define PTE_D 32

#define PF_P 1
#define PF_W 2
#define PF_U 4
#define PF_I 8

#define EXIT_FATAL 1
#define EXIT_INVALID 2
#define EXIT_TRIPLE 3
#define EXIT_DEADLOCK 4
#define EXIT_LIMIT 124

typedef struct {
	uint32_t PC;
	uint32_t registers[8];
	uint32_t SP;
	uint8_t *memory;
	uint8_t halted;
	uint8_t flags;
	uint8_t int_enabled;
	uint8_t int_pending;
	uint8_t int_vector;
	uint8_t mode;
	uint32_t syscall_pc;
	uint32_t disk_sector;
	uint32_t disk_buffer;
	uint32_t ram_size;
	uint32_t cr[16];
	uint64_t icount;
	uint64_t ilimit;
	int exit_code;
	uint32_t inst_pc;
	int depth;
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
	OP_JMPR = 0x41,
	OP_CALLR = 0x42,
	OP_JMPREL = 0x43,
	OP_CALLREL = 0x44,
	OP_BEQ = 0x45,
	OP_BNE = 0x46,
	OP_BLT = 0x47,
	OP_BGE = 0x48,
	OP_BLE = 0x49,
	OP_BGT = 0x4A,
	OP_BLO = 0x4B,
	OP_BHS = 0x4C,
	OP_BLS = 0x4D,
	OP_BHI = 0x4E,
	OP_BMI = 0x4F,
	OP_BPL = 0x50,
	OP_BVS = 0x51,
	OP_BVC = 0x52,
	OP_JMP32 = 0x53,
	OP_CALL32 = 0x54,
	OP_ADC = 0x55,
	OP_SBB = 0x56,
	OP_SARV = 0x57,
	OP_SHLV = 0x58,
	OP_SHRV = 0x59,
	OP_ROLV = 0x5A,
	OP_RORV = 0x5B,
	OP_IMUL = 0x5C,
	OP_IDIV = 0x5D,
	OP_UMOD = 0x5E,
	OP_IMOD = 0x5F,
	OP_NEG = 0x60,
	OP_TEST = 0x61,
	OP_MULHU = 0x62,
	OP_MULHS = 0x63,
	OP_SAR = 0x64,
	OP_ROL = 0x65,
	OP_ROR = 0x66,
	OP_ADCI = 0x67,
	OP_SBBI = 0x68,
	OP_IMULI = 0x69,
	OP_IDIVI = 0x6A,
	OP_UMODI = 0x6B,
	OP_IMODI = 0x6C,
	OP_TESTI = 0x6D,
	OP_LDB = 0x6E,
	OP_LDBS = 0x6F,
	OP_LDH = 0x70,
	OP_LDHS = 0x71,
	OP_LDW = 0x72,
	OP_STB = 0x73,
	OP_STH = 0x74,
	OP_STW = 0x75,
	OP_LEA = 0x76,
	OP_LEAPC = 0x77,
	OP_MOVSP = 0x78,
	OP_SETSP = 0x79,
	OP_ADDSP = 0x7A,
	OP_BRK = 0x7C,
	OP_MFCR = 0x7D,
	OP_MTCR = 0x7E,
	OP_TLBFLUSH = 0x7F,
	OP_INVLPG = 0x80,
	OP_WFI = 0x81,
	OP_CPUID = 0x82,
	OP_PUSHF = 0x83,
	OP_POPF = 0x84,
	OP_RDCYC = 0x85,
	OP_RET32 = 0x86,
	OP_PUSH32 = 0x87,
	OP_POP32 = 0x88,
	OP_ADD32R = 0x89,
	OP_SUB32R = 0x8A,
	OP_CMP32R = 0x8B,
	OP_MUL32R = 0x8C,
	OP_DIV32R = 0x8D,
	OP_HALT = 0xFF
} Opcode;

extern CPU cpu;
extern int cpu_verbose;
void cpu_init(void);
void cpu_init_size(uint32_t ram_size);
void cpu_run(void);
void cpu_step(void);
void load_program(const uint8_t *program, size_t size);
void cpu_interrupt(uint8_t vector);
int cpu_disassemble(uint32_t addr, char *out, size_t out_size);
void cpu_debug(void);
void cpu_stop(int code);
void cpu_fault(uint32_t vec, uint32_t code);
void cpu_deliver(uint32_t vec, uint32_t savepc, uint32_t code);
void cpu_invlpg(uint32_t va);
void cpu_wfi(void);
void cpu_bus_error(uint32_t addr);
void cpu_flush_tlb(void);
uint32_t cpu_get_flags(void);
void cpu_set_flags(uint32_t f);
uint32_t mem_read(uint32_t va, int n);
void mem_write(uint32_t va, uint32_t v, int n);
int mem_peek(uint32_t va, uint8_t *b);
uint32_t phys_read(uint32_t pa, int n);
void phys_write(uint32_t pa, uint32_t v, int n);
int rom_load(const char *path);
void cpu_dc_flush(void);
void cpu_set_dcache(int on);
void cpu_mem_written(uint32_t pa, uint32_t n);
void cpu_set_trace(FILE *f);
void cpu_trace_end(void);
int mem_poke(uint32_t va, uint8_t b);
int cpu_save(const char *path);
int cpu_load(const char *path);

#endif
