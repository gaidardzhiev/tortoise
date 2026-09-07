#include <stdio.h>
#include <stdlib.h>
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

static uint32_t fetch_dword(void) {
	uint32_t b0 = fetch_byte();
	uint32_t b1 = fetch_byte();
	uint32_t b2 = fetch_byte();
	uint32_t b3 = fetch_byte();
	return (b3 << 24) | (b2 << 16) | (b1 << 8) | b0;
}

void cpu_init(void) {
	memset(&cpu, 0, sizeof(cpu));
	cpu.PC = 0;
	cpu.SP = MEMORY_SIZE;
	cpu.halted = 0;
	cpu.int_enabled = 1;
	cpu.mode = MODE_SUPERVISOR;
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

void cpu_interrupt(uint8_t vector) {
	if (IVT_BASE + (uint32_t)vector * 2 + 1 >= MEMORY_SIZE) return;
	push_word(cpu.flags);
	push_word((uint16_t)cpu.PC);
	cpu.int_enabled = 0;
	uint8_t old_mode = cpu.mode;
	cpu.mode = MODE_SUPERVISOR;
	uint16_t low = cpu.memory[IVT_BASE + (uint32_t)vector * 2];
	uint16_t high = cpu.memory[IVT_BASE + (uint32_t)vector * 2 + 1];
	cpu.PC = (high << 8) | low;
	printf("interrupt: vector=%u PC=0x%X prev_mode=%u\n", vector, cpu.PC, old_mode);
}

static void set_flags(uint32_t a, uint32_t b, uint32_t result, uint8_t is_sub) {
	uint16_t r16 = result & 0xFFFF;
	cpu.flags = 0;
	if (r16 == 0)
		cpu.flags |= FLAG_Z;
	if (is_sub ? (b > a) : (result > 0xFFFF))
		cpu.flags |= FLAG_C;
	if (r16 & 0x8000)
		cpu.flags |= FLAG_S;
	uint8_t sa = (a >> 15) & 1;
	uint8_t sb = (b >> 15) & 1;
	uint8_t sr = (r16 >> 15) & 1;
	if (is_sub) {
		if (sa != sb && sr != sa)
			cpu.flags |= FLAG_V;
	} else {
		if (sa == sb && sr != sa)
			cpu.flags |= FLAG_V;
	}
}

static void execute_instruction(void) {
	if (cpu.PC >= MEMORY_SIZE) {
		fprintf(stderr, "PC out of bounds\n");
		cpu.halted = 1;
		return;
	}
	uint8_t opcode = fetch_byte();
	printf("executing opcode 0x%X at PC=0x%X\n", opcode, cpu.PC - 1);
	if (cpu.mode == MODE_USER) {
		if (opcode == OP_CLI || opcode == OP_STI || opcode == OP_IRET || opcode == OP_IN || opcode == OP_OUT) {
			fprintf(stderr, "privilege violation in user mode\n");
			cpu_interrupt(FAULT_GP);
			return;
		}
	}
	uint8_t reg, pair, dst, src;
	uint32_t val, addr, val32, addr32, eff;
	int16_t off;
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
		if (reg < 8) {
			uint32_t a = cpu.registers[reg];
			uint32_t result = a + val;
			set_flags(a, val, result, 0);
			cpu.registers[reg] = result & 0xFFFF;
		}
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
			cpu.registers[reg] = (uint16_t)(~cpu.registers[reg]);
			printf("NOT after: R%d=0x%X\n", reg, cpu.registers[reg]);
		}
		break;
	case OP_CALL:
		addr = fetch_word();
		push_word((uint16_t)cpu.PC);
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
		if (reg < 8) {
			if (addr == IO_CONSOLE_DATA) {
				cpu.registers[reg] = 'A';
			} else if (addr == IO_CONSOLE_STATUS) {
				cpu.registers[reg] = 0x03;
			} else if (addr == IO_TIMER) {
				static uint32_t timer_ticks = 0;
				cpu.registers[reg] = ++timer_ticks;
			} else if (addr == IO_RNG) {
				cpu.registers[reg] = (uint32_t)(rand() & 0xFFFF);
			} else if (addr == IO_SYS) {
				cpu.registers[reg] = cpu.halted;
			} else if (addr == IO_DISK_SECTOR) {
				cpu.registers[reg] = cpu.disk_sector;
			} else if (addr == IO_DISK_STATUS) {
				cpu.registers[reg] = 0x01;
			} else if (addr == IO_DISK_BUFFER) {
				cpu.registers[reg] = cpu.disk_buffer;
			}
		}
		break;
	case OP_OUT:
		reg = fetch_byte();
		addr = fetch_word();
		if (reg < 8) {
			if (addr == IO_CONSOLE_DATA) {
				putchar(cpu.registers[reg] & 0xFF);
				fflush(stdout);
			} else if (addr == IO_TIMER) {
				if (cpu.int_enabled) cpu_interrupt(1);
			} else if (addr == IO_SYS) {
				cpu.halted = 1;
			} else if (addr == IO_DISK_SECTOR) {
				cpu.disk_sector = cpu.registers[reg] & (DISK_NUM_SECTORS - 1);
			} else if (addr == IO_DISK_BUFFER) {
				cpu.disk_buffer = cpu.registers[reg];
			} else if (addr == IO_DISK_CMD) {
				if (cpu.registers[reg] == 1) {
					if (cpu.disk_buffer + DISK_SECTOR_SIZE <= MEMORY_SIZE) {
						memcpy(&cpu.memory[cpu.disk_buffer], &cpu.disk[cpu.disk_sector * DISK_SECTOR_SIZE], DISK_SECTOR_SIZE);
					}
				} else if (cpu.registers[reg] == 2) {
					if (cpu.disk_buffer + DISK_SECTOR_SIZE <= MEMORY_SIZE) {
						memcpy(&cpu.disk[cpu.disk_sector * DISK_SECTOR_SIZE], &cpu.memory[cpu.disk_buffer], DISK_SECTOR_SIZE);
					}
				}
			}
		}
		break;
	case OP_SUB:
		reg = fetch_byte();
		val = fetch_word();
		if (reg < 8) {
			uint32_t a = cpu.registers[reg];
			uint32_t result = (a - val) & 0xFFFF;
			set_flags(a, val, result, 1);
			cpu.registers[reg] = result;
			printf("SUB: R%d=0x%X - 0x%X = 0x%X flags=0x%X\n", reg, a, val, result, cpu.flags);
		}
		break;
	case OP_CMP:
		reg = fetch_byte();
		val = fetch_word();
		if (reg < 8) {
			uint32_t a = cpu.registers[reg];
			uint32_t result = (a - val) & 0xFFFF;
			set_flags(a, val, result, 1);
			printf("CMP: R%d=0x%X vs 0x%X flags=0x%X\n", reg, a, val, cpu.flags);
		}
		break;
	case OP_JC:
		addr = fetch_word();
		if ((cpu.flags & FLAG_C) && addr < MEMORY_SIZE) cpu.PC = addr;
		break;
	case OP_JN:
		addr = fetch_word();
		if ((cpu.flags & FLAG_S) && addr < MEMORY_SIZE) cpu.PC = addr;
		break;
	case OP_JO:
		addr = fetch_word();
		if ((cpu.flags & FLAG_V) && addr < MEMORY_SIZE) cpu.PC = addr;
		break;
	case OP_JNZ:
		addr = fetch_word();
		if (!(cpu.flags & FLAG_Z) && addr < MEMORY_SIZE) cpu.PC = addr;
		break;
	case OP_SHL:
		reg = fetch_byte();
		val = fetch_word();
		if (reg < 8) {
			uint32_t a = cpu.registers[reg];
			uint32_t res = (a << val) & 0xFFFF;
			cpu.flags = 0;
			if (res == 0) cpu.flags |= FLAG_Z;
			if (val > 0 && val <= 16 && ((a << (val - 1)) & 0x8000)) cpu.flags |= FLAG_C;
			if (res & 0x8000) cpu.flags |= FLAG_S;
			cpu.registers[reg] = res;
			printf("SHL: R%d=0x%X << %u = 0x%X flags=0x%X\n", reg, a, val, res, cpu.flags);
		}
		break;
	case OP_SHR:
		reg = fetch_byte();
		val = fetch_word();
		if (reg < 8) {
			uint32_t a = cpu.registers[reg];
			uint32_t res = (a >> val) & 0xFFFF;
			cpu.flags = 0;
			if (res == 0) cpu.flags |= FLAG_Z;
			if (val > 0 && val <= 16 && ((a >> (val - 1)) & 1)) cpu.flags |= FLAG_C;
			if (res & 0x8000) cpu.flags |= FLAG_S;
			cpu.registers[reg] = res;
			printf("SHR: R%d=0x%X >> %u = 0x%X flags=0x%X\n", reg, a, val, res, cpu.flags);
		}
		break;
	case OP_MUL:
		reg = fetch_byte();
		val = fetch_word();
		if (reg < 8) {
			uint32_t a = cpu.registers[reg];
			uint32_t prod = a * val;
			uint32_t res = prod & 0xFFFF;
			cpu.flags = 0;
			if (res == 0) cpu.flags |= FLAG_Z;
			if (prod > 0xFFFF) cpu.flags |= FLAG_C | FLAG_V;
			if (res & 0x8000) cpu.flags |= FLAG_S;
			cpu.registers[reg] = res;
			printf("MUL: R%d=0x%X * 0x%X = 0x%X flags=0x%X\n", reg, a, val, res, cpu.flags);
		}
		break;
	case OP_DIV:
		reg = fetch_byte();
		val = fetch_word();
		if (reg < 8) {
			if (val == 0) {
				fprintf(stderr, "division by zero\n");
				cpu_interrupt(0);
			} else {
				uint32_t a = cpu.registers[reg];
				uint32_t res = (a / val) & 0xFFFF;
				cpu.flags = 0;
				if (res == 0) cpu.flags |= FLAG_Z;
				if (res & 0x8000) cpu.flags |= FLAG_S;
				cpu.registers[reg] = res;
				printf("DIV: R%d=0x%X / 0x%X = 0x%X flags=0x%X\n", reg, a, val, res, cpu.flags);
			}
		}
		break;
	case OP_INT:
		val = fetch_byte();
		cpu_interrupt((uint8_t)val);
		break;
	case OP_IRET:
		addr = pop_word();
		cpu.flags = (uint8_t)pop_word();
		cpu.int_enabled = 1;
		printf("IRET to 0x%X flags=0x%X\n", addr, cpu.flags);
		cpu.PC = addr;
		break;
	case OP_CLI:
		cpu.int_enabled = 0;
		break;
	case OP_STI:
		cpu.int_enabled = 1;
		break;
	case OP_LOAD32:
		reg = fetch_byte();
		val32 = fetch_dword();
		if (reg < 8) cpu.registers[reg] = val32;
		break;
	case OP_STORE32:
		reg = fetch_byte();
		addr32 = fetch_dword();
		if (reg < 8 && addr32 + 3 < MEMORY_SIZE) {
			val32 = cpu.registers[reg];
			cpu.memory[addr32] = val32 & 0xFF;
			cpu.memory[addr32 + 1] = (val32 >> 8) & 0xFF;
			cpu.memory[addr32 + 2] = (val32 >> 16) & 0xFF;
			cpu.memory[addr32 + 3] = (val32 >> 24) & 0xFF;
		}
		break;
	case OP_ADD32:
		reg = fetch_byte();
		val32 = fetch_dword();
		if (reg < 8) {
			uint64_t a = cpu.registers[reg];
			uint64_t sum = a + (uint64_t)val32;
			cpu.flags = 0;
			if ((sum & 0xFFFFFFFF) == 0) cpu.flags |= FLAG_Z;
			if (sum > 0xFFFFFFFF) cpu.flags |= FLAG_C;
			if (sum & 0x80000000) cpu.flags |= FLAG_S;
			cpu.registers[reg] = (uint32_t)sum;
		}
		break;
	case OP_SUB32:
		reg = fetch_byte();
		val32 = fetch_dword();
		if (reg < 8) {
			uint32_t a = cpu.registers[reg];
			uint32_t diff = a - val32;
			cpu.flags = 0;
			if (diff == 0) cpu.flags |= FLAG_Z;
			if (val32 > a) cpu.flags |= FLAG_C;
			if (diff & 0x80000000) cpu.flags |= FLAG_S;
			cpu.registers[reg] = diff;
		}
		break;
	case OP_MUL32:
		reg = fetch_byte();
		val32 = fetch_dword();
		if (reg < 8) {
			uint64_t a = cpu.registers[reg];
			uint64_t prod = a * (uint64_t)val32;
			cpu.flags = 0;
			if ((prod & 0xFFFFFFFF) == 0) cpu.flags |= FLAG_Z;
			if (prod > 0xFFFFFFFF) cpu.flags |= FLAG_C | FLAG_V;
			if (prod & 0x80000000) cpu.flags |= FLAG_S;
			cpu.registers[reg] = (uint32_t)prod;
		}
		break;
	case OP_DIV32:
		reg = fetch_byte();
		val32 = fetch_dword();
		if (reg < 8) {
			if (val32 == 0) {
				fprintf(stderr, "division by zero\n");
				cpu_interrupt(0);
			} else {
				uint32_t a = cpu.registers[reg];
				uint32_t quot = a / val32;
				cpu.flags = 0;
				if (quot == 0) cpu.flags |= FLAG_Z;
				if (quot & 0x80000000) cpu.flags |= FLAG_S;
				cpu.registers[reg] = quot;
			}
		}
		break;
	case OP_SHL32:
		reg = fetch_byte();
		val32 = fetch_dword();
		if (reg < 8) {
			uint32_t a = cpu.registers[reg];
			uint32_t res = a << val32;
			cpu.flags = 0;
			if (res == 0) cpu.flags |= FLAG_Z;
			if (val32 > 0 && val32 <= 32 && ((a << (val32 - 1)) & 0x80000000)) cpu.flags |= FLAG_C;
			if (res & 0x80000000) cpu.flags |= FLAG_S;
			cpu.registers[reg] = res;
		}
		break;
	case OP_SHR32:
		reg = fetch_byte();
		val32 = fetch_dword();
		if (reg < 8) {
			uint32_t a = cpu.registers[reg];
			uint32_t res = a >> val32;
			cpu.flags = 0;
			if (res == 0) cpu.flags |= FLAG_Z;
			if (val32 > 0 && val32 <= 32 && ((a >> (val32 - 1)) & 1)) cpu.flags |= FLAG_C;
			if (res & 0x80000000) cpu.flags |= FLAG_S;
			cpu.registers[reg] = res;
		}
		break;
	case OP_AND32:
		reg = fetch_byte();
		val32 = fetch_dword();
		if (reg < 8) {
			cpu.registers[reg] &= val32;
		}
		break;
	case OP_OR32:
		reg = fetch_byte();
		val32 = fetch_dword();
		if (reg < 8) {
			cpu.registers[reg] |= val32;
		}
		break;
	case OP_XOR32:
		reg = fetch_byte();
		val32 = fetch_dword();
		if (reg < 8) {
			cpu.registers[reg] ^= val32;
		}
		break;
	case OP_CMP32:
		reg = fetch_byte();
		val32 = fetch_dword();
		if (reg < 8) {
			uint32_t a = cpu.registers[reg];
			uint32_t diff = a - val32;
			cpu.flags = 0;
			if (diff == 0) cpu.flags |= FLAG_Z;
			if (val32 > a) cpu.flags |= FLAG_C;
			if (diff & 0x80000000) cpu.flags |= FLAG_S;
		}
		break;
	case OP_MOV:
		pair = fetch_byte();
		dst = (pair >> 4) & 0x0F;
		src = pair & 0x0F;
		if (dst < 8 && src < 8) {
			cpu.registers[dst] = cpu.registers[src];
			printf("MOV: R%d = R%d (0x%X)\n", dst, src, cpu.registers[dst]);
		}
		break;
	case OP_ADD_REG:
		pair = fetch_byte();
		dst = (pair >> 4) & 0x0F;
		src = pair & 0x0F;
		if (dst < 8 && src < 8) {
			uint32_t a = cpu.registers[dst];
			uint32_t b = cpu.registers[src];
			uint32_t result = a + b;
			set_flags(a, b, result, 0);
			cpu.registers[dst] = result & 0xFFFF;
			printf("ADD_REG: R%d=0x%X + R%d=0x%X = 0x%X flags=0x%X\n", dst, a, src, b, cpu.registers[dst], cpu.flags);
		}
		break;
	case OP_SUB_REG:
		pair = fetch_byte();
		dst = (pair >> 4) & 0x0F;
		src = pair & 0x0F;
		if (dst < 8 && src < 8) {
			uint32_t a = cpu.registers[dst];
			uint32_t b = cpu.registers[src];
			uint32_t result = (a - b) & 0xFFFF;
			set_flags(a, b, result, 1);
			cpu.registers[dst] = result;
			printf("SUB_REG: R%d=0x%X - R%d=0x%X = 0x%X flags=0x%X\n", dst, a, src, b, result, cpu.flags);
		}
		break;
	case OP_CMP_REG:
		pair = fetch_byte();
		dst = (pair >> 4) & 0x0F;
		src = pair & 0x0F;
		if (dst < 8 && src < 8) {
			uint32_t a = cpu.registers[dst];
			uint32_t b = cpu.registers[src];
			uint32_t result = (a - b) & 0xFFFF;
			set_flags(a, b, result, 1);
			printf("CMP_REG: R%d=0x%X vs R%d=0x%X flags=0x%X\n", dst, a, src, b, cpu.flags);
		}
		break;
	case OP_AND_REG:
		pair = fetch_byte();
		dst = (pair >> 4) & 0x0F;
		src = pair & 0x0F;
		if (dst < 8 && src < 8) {
			cpu.registers[dst] &= cpu.registers[src];
		}
		break;
	case OP_OR_REG:
		pair = fetch_byte();
		dst = (pair >> 4) & 0x0F;
		src = pair & 0x0F;
		if (dst < 8 && src < 8) {
			cpu.registers[dst] |= cpu.registers[src];
		}
		break;
	case OP_XOR_REG:
		pair = fetch_byte();
		dst = (pair >> 4) & 0x0F;
		src = pair & 0x0F;
		if (dst < 8 && src < 8) {
			cpu.registers[dst] ^= cpu.registers[src];
		}
		break;
	case OP_MUL_REG:
		pair = fetch_byte();
		dst = (pair >> 4) & 0x0F;
		src = pair & 0x0F;
		if (dst < 8 && src < 8) {
			uint32_t a = cpu.registers[dst];
			uint32_t b = cpu.registers[src];
			uint32_t prod = a * b;
			uint32_t res = prod & 0xFFFF;
			cpu.flags = 0;
			if (res == 0) cpu.flags |= FLAG_Z;
			if (prod > 0xFFFF) cpu.flags |= FLAG_C | FLAG_V;
			if (res & 0x8000) cpu.flags |= FLAG_S;
			cpu.registers[dst] = res;
		}
		break;
	case OP_DIV_REG:
		pair = fetch_byte();
		dst = (pair >> 4) & 0x0F;
		src = pair & 0x0F;
		if (dst < 8 && src < 8) {
			uint32_t b = cpu.registers[src];
			if (b == 0) {
				fprintf(stderr, "division by zero\n");
				cpu_interrupt(0);
			} else {
				uint32_t a = cpu.registers[dst];
				uint32_t res = (a / b) & 0xFFFF;
				cpu.flags = 0;
				if (res == 0) cpu.flags |= FLAG_Z;
				if (res & 0x8000) cpu.flags |= FLAG_S;
				cpu.registers[dst] = res;
			}
		}
		break;
	case OP_PUSH:
		reg = fetch_byte();
		if (reg < 8) push_word((uint16_t)cpu.registers[reg]);
		break;
	case OP_POP:
		reg = fetch_byte();
		if (reg < 8) cpu.registers[reg] = pop_word();
		break;
	case OP_LOAD_INDIR:
		pair = fetch_byte();
		dst = (pair >> 4) & 0x0F;
		src = pair & 0x0F;
		off = (int16_t)fetch_word();
		if (dst < 8 && src < 8) {
			eff = (uint32_t)((int32_t)cpu.registers[src] + off);
			if (eff + 1 < MEMORY_SIZE) {
				uint16_t low = cpu.memory[eff];
				uint16_t high = cpu.memory[eff + 1];
				cpu.registers[dst] = (high << 8) | low;
				printf("LOAD_INDIR: R%d = [R%d + %d] (0x%X = 0x%X)\n", dst, src, off, eff, cpu.registers[dst]);
			}
		}
		break;
	case OP_STORE_INDIR:
		pair = fetch_byte();
		src = (pair >> 4) & 0x0F;
		reg = pair & 0x0F;
		off = (int16_t)fetch_word();
		if (src < 8 && reg < 8) {
			eff = (uint32_t)((int32_t)cpu.registers[src] + off);
			if (eff + 1 < MEMORY_SIZE) {
				val = cpu.registers[reg];
				cpu.memory[eff] = val & 0xFF;
				cpu.memory[eff + 1] = (val >> 8) & 0xFF;
				printf("STORE_INDIR: [R%d + %d] (0x%X) = R%d (0x%X)\n", src, off, eff, reg, val);
			}
		}
		break;
	case OP_LOAD32_INDIR:
		pair = fetch_byte();
		dst = (pair >> 4) & 0x0F;
		src = pair & 0x0F;
		off = (int16_t)fetch_word();
		if (dst < 8 && src < 8) {
			eff = (uint32_t)((int32_t)cpu.registers[src] + off);
			if (eff + 3 < MEMORY_SIZE) {
				uint32_t b0 = cpu.memory[eff];
				uint32_t b1 = cpu.memory[eff + 1];
				uint32_t b2 = cpu.memory[eff + 2];
				uint32_t b3 = cpu.memory[eff + 3];
				cpu.registers[dst] = (b3 << 24) | (b2 << 16) | (b1 << 8) | b0;
			}
		}
		break;
	case OP_STORE32_INDIR:
		pair = fetch_byte();
		src = (pair >> 4) & 0x0F;
		reg = pair & 0x0F;
		off = (int16_t)fetch_word();
		if (src < 8 && reg < 8) {
			eff = (uint32_t)((int32_t)cpu.registers[src] + off);
			if (eff + 3 < MEMORY_SIZE) {
				val32 = cpu.registers[reg];
				cpu.memory[eff] = val32 & 0xFF;
				cpu.memory[eff + 1] = (val32 >> 8) & 0xFF;
				cpu.memory[eff + 2] = (val32 >> 16) & 0xFF;
				cpu.memory[eff + 3] = (val32 >> 24) & 0xFF;
			}
		}
		break;
	case OP_SYSCALL:
		cpu.mode = MODE_SUPERVISOR;
		cpu.syscall_pc = cpu.PC;
		addr = (uint32_t)cpu.memory[IVT_BASE + 0x80] | ((uint32_t)cpu.memory[IVT_BASE + 0x81] << 8);
		printf("SYSCALL: user PC=0x%X jumping to 0x%X\n", cpu.syscall_pc, addr);
		cpu.PC = addr;
		break;
	case OP_SYSRET:
		cpu.mode = MODE_USER;
		if (cpu.syscall_pc != 0) {
			cpu.PC = cpu.syscall_pc;
			cpu.syscall_pc = 0;
		}
		printf("SYSRET: to user PC=0x%X\n", cpu.PC);
		break;
	case OP_HALT:
		cpu.halted = 1;
		break;
	default:
		printf("unknown opcode 0x%X at PC=0x%X\n", opcode, cpu.PC - 1);
		cpu.halted = 1;
		break;
	}
}

void cpu_run(void) {
	while (!cpu.halted) {
		execute_instruction();
	}
}

int cpu_disassemble(uint32_t addr, char *out, size_t out_size) {
	if (addr >= MEMORY_SIZE) {
		snprintf(out, out_size, "??? (out of bounds)");
		return 1;
	}
	uint8_t op = cpu.memory[addr];
	switch (op) {
	case OP_NOP: snprintf(out, out_size, "NOP"); return 1;
	case OP_HALT: snprintf(out, out_size, "HALT"); return 1;
	case OP_RET: snprintf(out, out_size, "RET"); return 1;
	case OP_CLI: snprintf(out, out_size, "CLI"); return 1;
	case OP_STI: snprintf(out, out_size, "STI"); return 1;
	case OP_IRET: snprintf(out, out_size, "IRET"); return 1;
	case OP_SYSCALL: snprintf(out, out_size, "SYSCALL"); return 1;
	case OP_SYSRET: snprintf(out, out_size, "SYSRET"); return 1;
	case OP_NOT: snprintf(out, out_size, "NOT R%d", cpu.memory[addr + 1]); return 2;
	case OP_INT: snprintf(out, out_size, "INT %u", cpu.memory[addr + 1]); return 2;
	case OP_PUSH: snprintf(out, out_size, "PUSH R%d", cpu.memory[addr + 1]); return 2;
	case OP_POP: snprintf(out, out_size, "POP R%d", cpu.memory[addr + 1]); return 2;
	case OP_MOV: snprintf(out, out_size, "MOV R%d, R%d", (cpu.memory[addr + 1] >> 4) & 0xF, cpu.memory[addr + 1] & 0xF); return 2;
	case OP_ADD_REG: snprintf(out, out_size, "ADD R%d, R%d", (cpu.memory[addr + 1] >> 4) & 0xF, cpu.memory[addr + 1] & 0xF); return 2;
	case OP_SUB_REG: snprintf(out, out_size, "SUB R%d, R%d", (cpu.memory[addr + 1] >> 4) & 0xF, cpu.memory[addr + 1] & 0xF); return 2;
	case OP_CMP_REG: snprintf(out, out_size, "CMP R%d, R%d", (cpu.memory[addr + 1] >> 4) & 0xF, cpu.memory[addr + 1] & 0xF); return 2;
	case OP_JMP: snprintf(out, out_size, "JMP 0x%04X", cpu.memory[addr + 1] | (cpu.memory[addr + 2] << 8)); return 3;
	case OP_CALL: snprintf(out, out_size, "CALL 0x%04X", cpu.memory[addr + 1] | (cpu.memory[addr + 2] << 8)); return 3;
	case OP_JC: snprintf(out, out_size, "JC 0x%04X", cpu.memory[addr + 1] | (cpu.memory[addr + 2] << 8)); return 3;
	case OP_JN: snprintf(out, out_size, "JN 0x%04X", cpu.memory[addr + 1] | (cpu.memory[addr + 2] << 8)); return 3;
	case OP_JO: snprintf(out, out_size, "JO 0x%04X", cpu.memory[addr + 1] | (cpu.memory[addr + 2] << 8)); return 3;
	case OP_JNZ: snprintf(out, out_size, "JNZ 0x%04X", cpu.memory[addr + 1] | (cpu.memory[addr + 2] << 8)); return 3;
	case OP_LOAD: snprintf(out, out_size, "LOAD R%d, 0x%04X", cpu.memory[addr + 1], cpu.memory[addr + 2] | (cpu.memory[addr + 3] << 8)); return 4;
	case OP_ADD: snprintf(out, out_size, "ADD R%d, 0x%04X", cpu.memory[addr + 1], cpu.memory[addr + 2] | (cpu.memory[addr + 3] << 8)); return 4;
	case OP_STORE: snprintf(out, out_size, "STORE R%d, 0x%04X", cpu.memory[addr + 1], cpu.memory[addr + 2] | (cpu.memory[addr + 3] << 8)); return 4;
	case OP_JZ: snprintf(out, out_size, "JZ R%d, 0x%04X", cpu.memory[addr + 1], cpu.memory[addr + 2] | (cpu.memory[addr + 3] << 8)); return 4;
	case OP_SUB: snprintf(out, out_size, "SUB R%d, 0x%04X", cpu.memory[addr + 1], cpu.memory[addr + 2] | (cpu.memory[addr + 3] << 8)); return 4;
	case OP_CMP: snprintf(out, out_size, "CMP R%d, 0x%04X", cpu.memory[addr + 1], cpu.memory[addr + 2] | (cpu.memory[addr + 3] << 8)); return 4;
	case OP_AND: snprintf(out, out_size, "AND R%d, 0x%04X", cpu.memory[addr + 1], cpu.memory[addr + 2] | (cpu.memory[addr + 3] << 8)); return 4;
	case OP_OR: snprintf(out, out_size, "OR R%d, 0x%04X", cpu.memory[addr + 1], cpu.memory[addr + 2] | (cpu.memory[addr + 3] << 8)); return 4;
	case OP_XOR: snprintf(out, out_size, "XOR R%d, 0x%04X", cpu.memory[addr + 1], cpu.memory[addr + 2] | (cpu.memory[addr + 3] << 8)); return 4;
	case OP_SHL: snprintf(out, out_size, "SHL R%d, %u", cpu.memory[addr + 1], cpu.memory[addr + 2] | (cpu.memory[addr + 3] << 8)); return 4;
	case OP_SHR: snprintf(out, out_size, "SHR R%d, %u", cpu.memory[addr + 1], cpu.memory[addr + 2] | (cpu.memory[addr + 3] << 8)); return 4;
	case OP_MUL: snprintf(out, out_size, "MUL R%d, %u", cpu.memory[addr + 1], cpu.memory[addr + 2] | (cpu.memory[addr + 3] << 8)); return 4;
	case OP_DIV: snprintf(out, out_size, "DIV R%d, %u", cpu.memory[addr + 1], cpu.memory[addr + 2] | (cpu.memory[addr + 3] << 8)); return 4;
	case OP_IN: snprintf(out, out_size, "IN R%d, 0x%04X", cpu.memory[addr + 1], cpu.memory[addr + 2] | (cpu.memory[addr + 3] << 8)); return 4;
	case OP_OUT: snprintf(out, out_size, "OUT R%d, 0x%04X", cpu.memory[addr + 1], cpu.memory[addr + 2] | (cpu.memory[addr + 3] << 8)); return 4;
	case OP_LOAD_INDIR: {
		int16_t off = (int16_t)(cpu.memory[addr + 2] | (cpu.memory[addr + 3] << 8));
		snprintf(out, out_size, "LOAD R%d, [R%d + %d]", (cpu.memory[addr + 1] >> 4) & 0xF, cpu.memory[addr + 1] & 0xF, off);
		return 4;
	}
	case OP_STORE_INDIR: {
		int16_t off = (int16_t)(cpu.memory[addr + 2] | (cpu.memory[addr + 3] << 8));
		snprintf(out, out_size, "STORE [R%d + %d], R%d", (cpu.memory[addr + 1] >> 4) & 0xF, off, cpu.memory[addr + 1] & 0xF);
		return 4;
	}
	case OP_LOAD32: snprintf(out, out_size, "LOAD32 R%d, 0x%08X", cpu.memory[addr + 1], (uint32_t)(cpu.memory[addr + 2] | (cpu.memory[addr + 3] << 8) | (cpu.memory[addr + 4] << 16) | (cpu.memory[addr + 5] << 24))); return 6;
	case OP_STORE32: snprintf(out, out_size, "STORE32 R%d, 0x%08X", cpu.memory[addr + 1], (uint32_t)(cpu.memory[addr + 2] | (cpu.memory[addr + 3] << 8) | (cpu.memory[addr + 4] << 16) | (cpu.memory[addr + 5] << 24))); return 6;
	default:
		snprintf(out, out_size, ".byte 0x%02X", op);
		return 1;
	}
}

void cpu_debug(void) {
	char line[128];
	uint32_t bp = 0xFFFFFFFF;
	printf("tortoise debugger ready. commands: s (step), c (continue), r (regs), b <addr>, d <addr> <n>, x <addr> <n>, q (quit)\n");
	while (!cpu.halted) {
		char dis[64];
		cpu_disassemble(cpu.PC, dis, sizeof(dis));
		printf("[0x%05X] %-24s > ", cpu.PC, dis);
		if (!fgets(line, sizeof(line), stdin)) break;
		char cmd = line[0];
		if (cmd == 's' || cmd == '\n') {
			execute_instruction();
		} else if (cmd == 'r') {
			printf("PC: 0x%05X  SP: 0x%05X  FLAGS: 0x%02X  MODE: %s\n", cpu.PC, cpu.SP, cpu.flags, cpu.mode == MODE_SUPERVISOR ? "SUPERVISOR" : "USER");
			for (int i = 0; i < 8; i++) {
				printf("R%d: 0x%08X (%u)  ", i, cpu.registers[i], cpu.registers[i]);
				if (i == 3 || i == 7) printf("\n");
			}
		} else if (cmd == 'b') {
			unsigned long baddr = 0;
			if (sscanf(line + 1, "%lx", &baddr) == 1) {
				bp = (uint32_t)baddr;
				printf("breakpoint set at 0x%05X\n", bp);
			}
		} else if (cmd == 'c') {
			while (!cpu.halted) {
				if (cpu.PC == bp) {
					printf("hit breakpoint at 0x%05X\n", bp);
					break;
				}
				execute_instruction();
			}
		} else if (cmd == 'd') {
			unsigned long daddr = cpu.PC;
			int cnt = 8;
			sscanf(line + 1, "%lx %d", &daddr, &cnt);
			uint32_t cur = (uint32_t)daddr;
			for (int i = 0; i < cnt && cur < MEMORY_SIZE; i++) {
				char buf[64];
				int sz = cpu_disassemble(cur, buf, sizeof(buf));
				printf("  0x%05X: %s\n", cur, buf);
				cur += sz;
			}
		} else if (cmd == 'x') {
			unsigned long xaddr = 0;
			int cnt = 16;
			sscanf(line + 1, "%lx %d", &xaddr, &cnt);
			for (int i = 0; i < cnt && (xaddr + i) < MEMORY_SIZE; i++) {
				if (i % 16 == 0) printf("  0x%05lX: ", xaddr + i);
				printf("%02X ", cpu.memory[xaddr + i]);
				if (i % 16 == 15 || i == cnt - 1) printf("\n");
			}
		} else if (cmd == 'q') {
			break;
		}
	}
}
