#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <strings.h>

#define MAX_LINE_LEN 256
#define MAX_CODE_SIZE 1048576

enum {
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
};

typedef struct {
	char name[64];
	uint32_t addr;
} Symbol;

static Symbol symbols[2048];
static int symbol_count = 0;

static void add_symbol(const char* name, uint32_t addr) {
	for (int i = 0; i < symbol_count; i++) {
		if (strcmp(symbols[i].name, name) == 0) {
			symbols[i].addr = addr;
			return;
		}
	}
	if (symbol_count < 2048) {
		strncpy(symbols[symbol_count].name, name, 63);
		symbols[symbol_count].name[63] = '\0';
		symbols[symbol_count].addr = addr;
		symbol_count++;
	}
}

static int lookup_symbol(const char* name, uint32_t* addr) {
	for (int i = 0; i < symbol_count; i++) {
		if (strcmp(symbols[i].name, name) == 0) {
			*addr = symbols[i].addr;
			return 1;
		}
	}
	return 0;
}

int parse_register(const char* token) {
	if (tolower((unsigned char)token[0]) == 'r' && isdigit((unsigned char)token[1])) {
		int reg = token[1] - '0';
		if (reg >= 0 && reg <= 7) return reg;
	}
	return -1;
}

int parse_immediate(const char* token, uint16_t* value) {
	while (isspace((unsigned char)*token)) token++;
	char* endptr = NULL;
	unsigned long val = 0;
	if (strncasecmp(token, "0x", 2) == 0) {
		val = strtoul(token + 2, &endptr, 16);
		while (isspace((unsigned char)*endptr)) endptr++;
		if (endptr != token + 2 && *endptr == '\0') {
			*value = (uint16_t)val;
			return 1;
		}
	} else if (isdigit((unsigned char)token[0]) || (token[0] == '-' && isdigit((unsigned char)token[1]))) {
		val = strtoul(token, &endptr, 10);
		while (isspace((unsigned char)*endptr)) endptr++;
		if (endptr != token && *endptr == '\0') {
			*value = (uint16_t)val;
			return 1;
		}
	}
	char sym[64];
	int si = 0;
	while (*token && !isspace((unsigned char)*token) && si < 63) sym[si++] = *token++;
	sym[si] = '\0';
	uint32_t addr = 0;
	if (lookup_symbol(sym, &addr)) {
		*value = (uint16_t)addr;
		return 1;
	}
	return 0;
}

int parse_immediate32(const char* token, uint32_t* value) {
	while (isspace((unsigned char)*token)) token++;
	char* endptr = NULL;
	unsigned long val = 0;
	if (strncasecmp(token, "0x", 2) == 0) {
		val = strtoul(token + 2, &endptr, 16);
		while (isspace((unsigned char)*endptr)) endptr++;
		if (endptr != token + 2 && *endptr == '\0') {
			*value = (uint32_t)val;
			return 1;
		}
	} else if (isdigit((unsigned char)token[0]) || (token[0] == '-' && isdigit((unsigned char)token[1]))) {
		val = strtoul(token, &endptr, 10);
		while (isspace((unsigned char)*endptr)) endptr++;
		if (endptr != token && *endptr == '\0') {
			*value = (uint32_t)val;
			return 1;
		}
	}
	char sym[64];
	int si = 0;
	while (*token && !isspace((unsigned char)*token) && si < 63) sym[si++] = *token++;
	sym[si] = '\0';
	uint32_t addr = 0;
	if (lookup_symbol(sym, &addr)) {
		*value = addr;
		return 1;
	}
	return 0;
}

void write_word(uint8_t* buffer, int offset, uint16_t val) {
	buffer[offset] = val & 0xFF;
	buffer[offset + 1] = (val >> 8) & 0xFF;
}

void write_dword(uint8_t* buffer, int offset, uint32_t val) {
	buffer[offset] = val & 0xFF;
	buffer[offset + 1] = (val >> 8) & 0xFF;
	buffer[offset + 2] = (val >> 16) & 0xFF;
	buffer[offset + 3] = (val >> 24) & 0xFF;
}

static int parse_indirect(const char* token, int* base_reg, int16_t* offset) {
	const char* p = token;
	while (isspace((unsigned char)*p)) p++;
	if (*p != '[') return 0;
	p++;
	while (isspace((unsigned char)*p)) p++;
	char reg_str[8] = {0};
	int i = 0;
	while (isalnum((unsigned char)*p) && i < 7) reg_str[i++] = *p++;
	reg_str[i] = '\0';
	*base_reg = parse_register(reg_str);
	if (*base_reg == -1) return 0;
	while (isspace((unsigned char)*p)) p++;
	int16_t off = 0;
	if (*p == '+' || *p == '-') {
		char sign = *p++;
		while (isspace((unsigned char)*p)) p++;
		char num_str[32] = {0};
		int j = 0;
		while (*p != ']' && *p != '\0' && j < 31) {
			if (!isspace((unsigned char)*p)) num_str[j++] = *p;
			p++;
		}
		num_str[j] = '\0';
		uint16_t uval = 0;
		if (!parse_immediate(num_str, &uval)) return 0;
		off = (sign == '-') ? (int16_t)(-(int32_t)uval) : (int16_t)uval;
	}
	while (isspace((unsigned char)*p)) p++;
	if (*p != ']') return 0;
	*offset = off;
	return 1;
}

static char* extract_instruction_start(char* line_copy, int pass, int* offset) {
	char* comment_pos = strchr(line_copy, ';');
	if (comment_pos) *comment_pos = '\0';
	char* start = line_copy;
	while (isspace((unsigned char)*start)) start++;
	if (*start == '\0') return NULL;
	char* colon = strchr(start, ':');
	if (colon) {
		*colon = '\0';
		char label[64];
		sscanf(start, "%63s", label);
		if (pass == 1) {
			add_symbol(label, *offset);
		}
		start = colon + 1;
		while (isspace((unsigned char)*start)) start++;
		if (*start == '\0') return NULL;
	}
	return start;
}

int assemble_line_pass1(const char* line, int offset) {
	char line_copy[MAX_LINE_LEN];
	strncpy(line_copy, line, MAX_LINE_LEN - 1);
	line_copy[MAX_LINE_LEN - 1] = '\0';
	char* start = extract_instruction_start(line_copy, 1, &offset);
	if (!start) return offset;

	if (start[0] == '.') {
		char dir[32] = {0};
		sscanf(start, "%31s", dir);
		for (int i = 0; dir[i]; i++) dir[i] = toupper((unsigned char)dir[i]);
		char* rest = start + strlen(dir);
		while (isspace((unsigned char)*rest)) rest++;
		if (strcmp(dir, ".ORG") == 0) {
			uint32_t org_addr = 0;
			parse_immediate32(rest, &org_addr);
			return (int)org_addr;
		} else if (strcmp(dir, ".BYTE") == 0) {
			int cnt = 0;
			char* tok = strtok(rest, ",");
			while (tok) { cnt++; tok = strtok(NULL, ","); }
			return offset + cnt;
		} else if (strcmp(dir, ".WORD") == 0) {
			int cnt = 0;
			char* tok = strtok(rest, ",");
			while (tok) { cnt++; tok = strtok(NULL, ","); }
			return offset + cnt * 2;
		} else if (strcmp(dir, ".DWORD") == 0) {
			int cnt = 0;
			char* tok = strtok(rest, ",");
			while (tok) { cnt++; tok = strtok(NULL, ","); }
			return offset + cnt * 4;
		} else if (strcmp(dir, ".ASCIIZ") == 0 || strcmp(dir, ".ASCII") == 0) {
			char* q1 = strchr(rest, '"');
			if (!q1) return offset;
			char* q2 = strchr(q1 + 1, '"');
			if (!q2) return offset;
			int len = (int)(q2 - (q1 + 1));
			return offset + len + (strcmp(dir, ".ASCIIZ") == 0 ? 1 : 0);
		}
		return offset;
	}

	char instr[32] = {0};
	char op1[64] = {0};
	char op2[64] = {0};
	sscanf(start, "%31s %63[^,], %63[^\n\r]", instr, op1, op2);
	for (int i = 0; instr[i]; i++) instr[i] = toupper((unsigned char)instr[i]);

	if (strcmp(instr, "NOP") == 0 || strcmp(instr, "HALT") == 0 || strcmp(instr, "RET") == 0 ||
	    strcmp(instr, "CLI") == 0 || strcmp(instr, "STI") == 0 || strcmp(instr, "IRET") == 0 ||
	    strcmp(instr, "SYSCALL") == 0 || strcmp(instr, "SYSRET") == 0) {
		return offset + 1;
	}
	if (strcmp(instr, "NOT") == 0 || strcmp(instr, "INT") == 0 || strcmp(instr, "PUSH") == 0 || strcmp(instr, "POP") == 0) {
		return offset + 2;
	}
	if (strcmp(instr, "MOV") == 0) {
		return offset + 2;
	}
	if (strcmp(instr, "ADD") == 0 || strcmp(instr, "SUB") == 0 || strcmp(instr, "CMP") == 0 ||
	    strcmp(instr, "AND") == 0 || strcmp(instr, "OR") == 0 || strcmp(instr, "XOR") == 0 ||
	    strcmp(instr, "MUL") == 0 || strcmp(instr, "DIV") == 0) {
		char* p2 = op2;
		while (isspace((unsigned char)*p2)) p2++;
		if (parse_register(p2) != -1) return offset + 2;
		return offset + 4;
	}
	if (strcmp(instr, "JMP") == 0 || strcmp(instr, "CALL") == 0 || strcmp(instr, "JC") == 0 ||
	    strcmp(instr, "JN") == 0 || strcmp(instr, "JO") == 0 || strcmp(instr, "JNZ") == 0) {
		return offset + 3;
	}
	if (strcmp(instr, "LOAD") == 0 || strcmp(instr, "STORE") == 0) {
		return offset + 4;
	}
	if (strcmp(instr, "JZ") == 0 || strcmp(instr, "IN") == 0 || strcmp(instr, "OUT") == 0 ||
	    strcmp(instr, "SHL") == 0 || strcmp(instr, "SHR") == 0) {
		return offset + 4;
	}
	if (strcmp(instr, "LOAD32") == 0 || strcmp(instr, "STORE32") == 0) {
		if (strchr(start, '[')) return offset + 4;
		return offset + 6;
	}
	if (strcmp(instr, "ADD32") == 0 || strcmp(instr, "SUB32") == 0 || strcmp(instr, "MUL32") == 0 ||
	    strcmp(instr, "DIV32") == 0 || strcmp(instr, "SHL32") == 0 || strcmp(instr, "SHR32") == 0 ||
	    strcmp(instr, "AND32") == 0 || strcmp(instr, "OR32") == 0 || strcmp(instr, "XOR32") == 0 ||
	    strcmp(instr, "CMP32") == 0) {
		return offset + 6;
	}
	fprintf(stderr, "unknown instruction: %s\n", instr);
	return -1;
}

int assemble_line(const char* line, uint8_t* buffer, int offset) {
	char line_copy[MAX_LINE_LEN];
	strncpy(line_copy, line, MAX_LINE_LEN - 1);
	line_copy[MAX_LINE_LEN - 1] = '\0';
	char* start = extract_instruction_start(line_copy, 2, &offset);
	if (!start) return offset;

	if (start[0] == '.') {
		char dir[32] = {0};
		sscanf(start, "%31s", dir);
		for (int i = 0; dir[i]; i++) dir[i] = toupper((unsigned char)dir[i]);
		char* rest = start + strlen(dir);
		while (isspace((unsigned char)*rest)) rest++;
		if (strcmp(dir, ".ORG") == 0) {
			uint32_t org_addr = 0;
			parse_immediate32(rest, &org_addr);
			return (int)org_addr;
		} else if (strcmp(dir, ".BYTE") == 0) {
			char* tok = strtok(rest, ",");
			while (tok) {
				uint16_t bval = 0;
				while (isspace((unsigned char)*tok)) tok++;
				parse_immediate(tok, &bval);
				buffer[offset++] = (uint8_t)bval;
				tok = strtok(NULL, ",");
			}
			return offset;
		} else if (strcmp(dir, ".WORD") == 0) {
			char* tok = strtok(rest, ",");
			while (tok) {
				uint16_t wval = 0;
				while (isspace((unsigned char)*tok)) tok++;
				parse_immediate(tok, &wval);
				write_word(buffer, offset, wval);
				offset += 2;
				tok = strtok(NULL, ",");
			}
			return offset;
		} else if (strcmp(dir, ".DWORD") == 0) {
			char* tok = strtok(rest, ",");
			while (tok) {
				uint32_t dwval = 0;
				while (isspace((unsigned char)*tok)) tok++;
				parse_immediate32(tok, &dwval);
				write_dword(buffer, offset, dwval);
				offset += 4;
				tok = strtok(NULL, ",");
			}
			return offset;
		} else if (strcmp(dir, ".ASCIIZ") == 0 || strcmp(dir, ".ASCII") == 0) {
			char* q1 = strchr(rest, '"');
			if (!q1) return offset;
			char* q2 = strchr(q1 + 1, '"');
			if (!q2) return offset;
			for (char* cp = q1 + 1; cp < q2; cp++) {
				buffer[offset++] = (uint8_t)*cp;
			}
			if (strcmp(dir, ".ASCIIZ") == 0) buffer[offset++] = '\0';
			return offset;
		}
		return offset;
	}

	char instr[32] = {0};
	char op1[64] = {0};
	char op2[64] = {0};
	int args = sscanf(start, "%31s %63[^,], %63[^\n\r]", instr, op1, op2);
	if (args < 1) {
		fprintf(stderr, "no instruction found in line: %s\n", line);
		return -1;
	}
	for (int i = 0; instr[i]; i++) instr[i] = toupper((unsigned char)instr[i]);
	char* p1 = op1;
	while (isspace((unsigned char)*p1)) p1++;
	char* p2 = op2;
	while (isspace((unsigned char)*p2)) p2++;
	char* end1 = p1 + strlen(p1);
	while (end1 > p1 && isspace((unsigned char)*(end1 - 1))) *(--end1) = '\0';
	char* end2 = p2 + strlen(p2);
	while (end2 > p2 && isspace((unsigned char)*(end2 - 1))) *(--end2) = '\0';

	int reg, base_reg;
	uint16_t val, addr;
	uint32_t val32, addr32;
	int16_t ind_off = 0;

	if (strcmp(instr, "NOP") == 0) {
		buffer[offset++] = OP_NOP;
		return offset;
	} else if (strcmp(instr, "HALT") == 0) {
		buffer[offset++] = OP_HALT;
		return offset;
	} else if (strcmp(instr, "RET") == 0) {
		buffer[offset++] = OP_RET;
		return offset;
	} else if (strcmp(instr, "CLI") == 0) {
		buffer[offset++] = OP_CLI;
		return offset;
	} else if (strcmp(instr, "STI") == 0) {
		buffer[offset++] = OP_STI;
		return offset;
	} else if (strcmp(instr, "IRET") == 0) {
		buffer[offset++] = OP_IRET;
		return offset;
	} else if (strcmp(instr, "SYSCALL") == 0) {
		buffer[offset++] = OP_SYSCALL;
		return offset;
	} else if (strcmp(instr, "SYSRET") == 0) {
		buffer[offset++] = OP_SYSRET;
		return offset;
	} else if (strcmp(instr, "PUSH") == 0) {
		if (sscanf(start, "%*s %63s", p1) < 1) return -1;
		reg = parse_register(p1);
		if (reg == -1) return -1;
		buffer[offset++] = OP_PUSH;
		buffer[offset++] = (uint8_t)reg;
		return offset;
	} else if (strcmp(instr, "POP") == 0) {
		if (sscanf(start, "%*s %63s", p1) < 1) return -1;
		reg = parse_register(p1);
		if (reg == -1) return -1;
		buffer[offset++] = OP_POP;
		buffer[offset++] = (uint8_t)reg;
		return offset;
	} else if (strcmp(instr, "MOV") == 0) {
		int r1 = parse_register(p1);
		int r2 = parse_register(p2);
		if (r1 == -1 || r2 == -1) return -1;
		buffer[offset++] = OP_MOV;
		buffer[offset++] = (uint8_t)((r1 << 4) | (r2 & 0x0F));
		return offset;
	} else if (strcmp(instr, "LOAD") == 0) {
		if (args < 3) return -1;
		reg = parse_register(p1);
		if (reg == -1) return -1;
		if (parse_indirect(p2, &base_reg, &ind_off)) {
			buffer[offset++] = OP_LOAD_INDIR;
			buffer[offset++] = (uint8_t)((reg << 4) | (base_reg & 0x0F));
			write_word(buffer, offset, (uint16_t)ind_off);
			offset += 2;
			return offset;
		}
		if (!parse_immediate(p2, &val)) return -1;
		buffer[offset++] = OP_LOAD;
		buffer[offset++] = (uint8_t)reg;
		write_word(buffer, offset, val);
		offset += 2;
		return offset;
	} else if (strcmp(instr, "STORE") == 0) {
		if (args < 3) return -1;
		if (parse_indirect(p1, &base_reg, &ind_off)) {
			reg = parse_register(p2);
			if (reg == -1) return -1;
			buffer[offset++] = OP_STORE_INDIR;
			buffer[offset++] = (uint8_t)((base_reg << 4) | (reg & 0x0F));
			write_word(buffer, offset, (uint16_t)ind_off);
			offset += 2;
			return offset;
		}
		reg = parse_register(p1);
		if (reg == -1 || !parse_immediate(p2, &addr)) return -1;
		buffer[offset++] = OP_STORE;
		buffer[offset++] = (uint8_t)reg;
		write_word(buffer, offset, addr);
		offset += 2;
		return offset;
	} else if (strcmp(instr, "LOAD32") == 0) {
		if (args < 3) return -1;
		reg = parse_register(p1);
		if (reg == -1) return -1;
		if (parse_indirect(p2, &base_reg, &ind_off)) {
			buffer[offset++] = OP_LOAD32_INDIR;
			buffer[offset++] = (uint8_t)((reg << 4) | (base_reg & 0x0F));
			write_word(buffer, offset, (uint16_t)ind_off);
			offset += 2;
			return offset;
		}
		if (!parse_immediate32(p2, &val32)) return -1;
		buffer[offset++] = OP_LOAD32;
		buffer[offset++] = (uint8_t)reg;
		write_dword(buffer, offset, val32);
		offset += 4;
		return offset;
	} else if (strcmp(instr, "STORE32") == 0) {
		if (args < 3) return -1;
		if (parse_indirect(p1, &base_reg, &ind_off)) {
			reg = parse_register(p2);
			if (reg == -1) return -1;
			buffer[offset++] = OP_STORE32_INDIR;
			buffer[offset++] = (uint8_t)((base_reg << 4) | (reg & 0x0F));
			write_word(buffer, offset, (uint16_t)ind_off);
			offset += 2;
			return offset;
		}
		reg = parse_register(p1);
		if (reg == -1 || !parse_immediate32(p2, &addr32)) return -1;
		buffer[offset++] = OP_STORE32;
		buffer[offset++] = (uint8_t)reg;
		write_dword(buffer, offset, addr32);
		offset += 4;
		return offset;
	} else if (strcmp(instr, "ADD") == 0) {
		int r1 = parse_register(p1);
		int r2 = parse_register(p2);
		if (r1 != -1 && r2 != -1) {
			buffer[offset++] = OP_ADD_REG;
			buffer[offset++] = (uint8_t)((r1 << 4) | (r2 & 0x0F));
			return offset;
		}
		if (r1 == -1 || !parse_immediate(p2, &val)) return -1;
		buffer[offset++] = OP_ADD;
		buffer[offset++] = (uint8_t)r1;
		write_word(buffer, offset, val);
		offset += 2;
		return offset;
	} else if (strcmp(instr, "SUB") == 0) {
		int r1 = parse_register(p1);
		int r2 = parse_register(p2);
		if (r1 != -1 && r2 != -1) {
			buffer[offset++] = OP_SUB_REG;
			buffer[offset++] = (uint8_t)((r1 << 4) | (r2 & 0x0F));
			return offset;
		}
		if (r1 == -1 || !parse_immediate(p2, &val)) return -1;
		buffer[offset++] = OP_SUB;
		buffer[offset++] = (uint8_t)r1;
		write_word(buffer, offset, val);
		offset += 2;
		return offset;
	} else if (strcmp(instr, "CMP") == 0) {
		int r1 = parse_register(p1);
		int r2 = parse_register(p2);
		if (r1 != -1 && r2 != -1) {
			buffer[offset++] = OP_CMP_REG;
			buffer[offset++] = (uint8_t)((r1 << 4) | (r2 & 0x0F));
			return offset;
		}
		if (r1 == -1 || !parse_immediate(p2, &val)) return -1;
		buffer[offset++] = OP_CMP;
		buffer[offset++] = (uint8_t)r1;
		write_word(buffer, offset, val);
		offset += 2;
		return offset;
	} else if (strcmp(instr, "AND") == 0) {
		int r1 = parse_register(p1);
		int r2 = parse_register(p2);
		if (r1 != -1 && r2 != -1) {
			buffer[offset++] = OP_AND_REG;
			buffer[offset++] = (uint8_t)((r1 << 4) | (r2 & 0x0F));
			return offset;
		}
		if (r1 == -1 || !parse_immediate(p2, &val)) return -1;
		buffer[offset++] = OP_AND;
		buffer[offset++] = (uint8_t)r1;
		write_word(buffer, offset, val);
		offset += 2;
		return offset;
	} else if (strcmp(instr, "OR") == 0) {
		int r1 = parse_register(p1);
		int r2 = parse_register(p2);
		if (r1 != -1 && r2 != -1) {
			buffer[offset++] = OP_OR_REG;
			buffer[offset++] = (uint8_t)((r1 << 4) | (r2 & 0x0F));
			return offset;
		}
		if (r1 == -1 || !parse_immediate(p2, &val)) return -1;
		buffer[offset++] = OP_OR;
		buffer[offset++] = (uint8_t)r1;
		write_word(buffer, offset, val);
		offset += 2;
		return offset;
	} else if (strcmp(instr, "XOR") == 0) {
		int r1 = parse_register(p1);
		int r2 = parse_register(p2);
		if (r1 != -1 && r2 != -1) {
			buffer[offset++] = OP_XOR_REG;
			buffer[offset++] = (uint8_t)((r1 << 4) | (r2 & 0x0F));
			return offset;
		}
		if (r1 == -1 || !parse_immediate(p2, &val)) return -1;
		buffer[offset++] = OP_XOR;
		buffer[offset++] = (uint8_t)r1;
		write_word(buffer, offset, val);
		offset += 2;
		return offset;
	} else if (strcmp(instr, "MUL") == 0) {
		int r1 = parse_register(p1);
		int r2 = parse_register(p2);
		if (r1 != -1 && r2 != -1) {
			buffer[offset++] = OP_MUL_REG;
			buffer[offset++] = (uint8_t)((r1 << 4) | (r2 & 0x0F));
			return offset;
		}
		if (r1 == -1 || !parse_immediate(p2, &val)) return -1;
		buffer[offset++] = OP_MUL;
		buffer[offset++] = (uint8_t)r1;
		write_word(buffer, offset, val);
		offset += 2;
		return offset;
	} else if (strcmp(instr, "DIV") == 0) {
		int r1 = parse_register(p1);
		int r2 = parse_register(p2);
		if (r1 != -1 && r2 != -1) {
			buffer[offset++] = OP_DIV_REG;
			buffer[offset++] = (uint8_t)((r1 << 4) | (r2 & 0x0F));
			return offset;
		}
		if (r1 == -1 || !parse_immediate(p2, &val)) return -1;
		buffer[offset++] = OP_DIV;
		buffer[offset++] = (uint8_t)r1;
		write_word(buffer, offset, val);
		offset += 2;
		return offset;
	} else if (strcmp(instr, "JMP") == 0) {
		if (sscanf(start, "%*s %63s", p1) < 1 || !parse_immediate(p1, &addr)) return -1;
		buffer[offset++] = OP_JMP;
		write_word(buffer, offset, addr);
		offset += 2;
		return offset;
	} else if (strcmp(instr, "JZ") == 0) {
		if (args < 3) return -1;
		reg = parse_register(p1);
		if (reg == -1 || !parse_immediate(p2, &addr)) return -1;
		buffer[offset++] = OP_JZ;
		buffer[offset++] = (uint8_t)reg;
		write_word(buffer, offset, addr);
		offset += 2;
		return offset;
	} else if (strcmp(instr, "NOT") == 0) {
		if (sscanf(start, "%*s %63s", p1) < 1) return -1;
		reg = parse_register(p1);
		if (reg == -1) return -1;
		buffer[offset++] = OP_NOT;
		buffer[offset++] = (uint8_t)reg;
		return offset;
	} else if (strcmp(instr, "CALL") == 0) {
		if (sscanf(start, "%*s %63s", p1) < 1 || !parse_immediate(p1, &addr)) return -1;
		buffer[offset++] = OP_CALL;
		write_word(buffer, offset, addr);
		offset += 2;
		return offset;
	} else if (strcmp(instr, "IN") == 0) {
		if (args < 3) return -1;
		reg = parse_register(p1);
		if (reg == -1 || !parse_immediate(p2, &val)) return -1;
		buffer[offset++] = OP_IN;
		buffer[offset++] = (uint8_t)reg;
		write_word(buffer, offset, val);
		offset += 2;
		return offset;
	} else if (strcmp(instr, "OUT") == 0) {
		if (args < 3) return -1;
		reg = parse_register(p1);
		if (reg == -1 || !parse_immediate(p2, &val)) return -1;
		buffer[offset++] = OP_OUT;
		buffer[offset++] = (uint8_t)reg;
		write_word(buffer, offset, val);
		offset += 2;
		return offset;
	} else if (strcmp(instr, "JC") == 0) {
		if (sscanf(start, "%*s %63s", p1) < 1 || !parse_immediate(p1, &addr)) return -1;
		buffer[offset++] = OP_JC;
		write_word(buffer, offset, addr);
		offset += 2;
		return offset;
	} else if (strcmp(instr, "JN") == 0) {
		if (sscanf(start, "%*s %63s", p1) < 1 || !parse_immediate(p1, &addr)) return -1;
		buffer[offset++] = OP_JN;
		write_word(buffer, offset, addr);
		offset += 2;
		return offset;
	} else if (strcmp(instr, "JO") == 0) {
		if (sscanf(start, "%*s %63s", p1) < 1 || !parse_immediate(p1, &addr)) return -1;
		buffer[offset++] = OP_JO;
		write_word(buffer, offset, addr);
		offset += 2;
		return offset;
	} else if (strcmp(instr, "JNZ") == 0) {
		if (sscanf(start, "%*s %63s", p1) < 1 || !parse_immediate(p1, &addr)) return -1;
		buffer[offset++] = OP_JNZ;
		write_word(buffer, offset, addr);
		offset += 2;
		return offset;
	} else if (strcmp(instr, "SHL") == 0) {
		if (args < 3) return -1;
		reg = parse_register(p1);
		if (reg == -1 || !parse_immediate(p2, &val)) return -1;
		buffer[offset++] = OP_SHL;
		buffer[offset++] = (uint8_t)reg;
		write_word(buffer, offset, val);
		offset += 2;
		return offset;
	} else if (strcmp(instr, "SHR") == 0) {
		if (args < 3) return -1;
		reg = parse_register(p1);
		if (reg == -1 || !parse_immediate(p2, &val)) return -1;
		buffer[offset++] = OP_SHR;
		buffer[offset++] = (uint8_t)reg;
		write_word(buffer, offset, val);
		offset += 2;
		return offset;
	} else if (strcmp(instr, "INT") == 0) {
		if (sscanf(start, "%*s %63s", p1) < 1 || !parse_immediate(p1, &val)) return -1;
		buffer[offset++] = OP_INT;
		buffer[offset++] = (uint8_t)val;
		return offset;
	} else if (strcmp(instr, "ADD32") == 0) {
		if (args < 3) return -1;
		reg = parse_register(p1);
		if (reg == -1 || !parse_immediate32(p2, &val32)) return -1;
		buffer[offset++] = OP_ADD32;
		buffer[offset++] = (uint8_t)reg;
		write_dword(buffer, offset, val32);
		offset += 4;
		return offset;
	} else if (strcmp(instr, "SUB32") == 0) {
		if (args < 3) return -1;
		reg = parse_register(p1);
		if (reg == -1 || !parse_immediate32(p2, &val32)) return -1;
		buffer[offset++] = OP_SUB32;
		buffer[offset++] = (uint8_t)reg;
		write_dword(buffer, offset, val32);
		offset += 4;
		return offset;
	} else if (strcmp(instr, "MUL32") == 0) {
		if (args < 3) return -1;
		reg = parse_register(p1);
		if (reg == -1 || !parse_immediate32(p2, &val32)) return -1;
		buffer[offset++] = OP_MUL32;
		buffer[offset++] = (uint8_t)reg;
		write_dword(buffer, offset, val32);
		offset += 4;
		return offset;
	} else if (strcmp(instr, "DIV32") == 0) {
		if (args < 3) return -1;
		reg = parse_register(p1);
		if (reg == -1 || !parse_immediate32(p2, &val32)) return -1;
		buffer[offset++] = OP_DIV32;
		buffer[offset++] = (uint8_t)reg;
		write_dword(buffer, offset, val32);
		offset += 4;
		return offset;
	} else if (strcmp(instr, "SHL32") == 0) {
		if (args < 3) return -1;
		reg = parse_register(p1);
		if (reg == -1 || !parse_immediate32(p2, &val32)) return -1;
		buffer[offset++] = OP_SHL32;
		buffer[offset++] = (uint8_t)reg;
		write_dword(buffer, offset, val32);
		offset += 4;
		return offset;
	} else if (strcmp(instr, "SHR32") == 0) {
		if (args < 3) return -1;
		reg = parse_register(p1);
		if (reg == -1 || !parse_immediate32(p2, &val32)) return -1;
		buffer[offset++] = OP_SHR32;
		buffer[offset++] = (uint8_t)reg;
		write_dword(buffer, offset, val32);
		offset += 4;
		return offset;
	} else if (strcmp(instr, "AND32") == 0) {
		if (args < 3) return -1;
		reg = parse_register(p1);
		if (reg == -1 || !parse_immediate32(p2, &val32)) return -1;
		buffer[offset++] = OP_AND32;
		buffer[offset++] = (uint8_t)reg;
		write_dword(buffer, offset, val32);
		offset += 4;
		return offset;
	} else if (strcmp(instr, "OR32") == 0) {
		if (args < 3) return -1;
		reg = parse_register(p1);
		if (reg == -1 || !parse_immediate32(p2, &val32)) return -1;
		buffer[offset++] = OP_OR32;
		buffer[offset++] = (uint8_t)reg;
		write_dword(buffer, offset, val32);
		offset += 4;
		return offset;
	} else if (strcmp(instr, "XOR32") == 0) {
		if (args < 3) return -1;
		reg = parse_register(p1);
		if (reg == -1 || !parse_immediate32(p2, &val32)) return -1;
		buffer[offset++] = OP_XOR32;
		buffer[offset++] = (uint8_t)reg;
		write_dword(buffer, offset, val32);
		offset += 4;
		return offset;
	} else if (strcmp(instr, "CMP32") == 0) {
		if (args < 3) return -1;
		reg = parse_register(p1);
		if (reg == -1 || !parse_immediate32(p2, &val32)) return -1;
		buffer[offset++] = OP_CMP32;
		buffer[offset++] = (uint8_t)reg;
		write_dword(buffer, offset, val32);
		offset += 4;
		return offset;
	} else {
		fprintf(stderr, "unknown or malformed instruction: %s\n", line);
		return -1;
	}
}

int main(int argc, char* argv[]) {
	if (argc != 3) {
		fprintf(stderr, "usage: %s in.asm out.bin\n", argv[0]);
		return 1;
	}
	FILE* fin = fopen(argv[1], "r");
	if (!fin) {
		perror("fopen input");
		return 1;
	}
	char lines[16384][MAX_LINE_LEN];
	int total_lines = 0;
	while (total_lines < 16384 && fgets(lines[total_lines], sizeof(lines[0]), fin)) {
		total_lines++;
	}
	fclose(fin);

	int offset = 0;
	for (int i = 0; i < total_lines; i++) {
		int new_offset = assemble_line_pass1(lines[i], offset);
		if (new_offset < 0) {
			fprintf(stderr, "pass 1 failed at line: %s\n", lines[i]);
			return 1;
		}
		offset = new_offset;
	}

	FILE* fout = fopen(argv[2], "wb");
	if (!fout) {
		perror("fopen output");
		return 1;
	}
	uint8_t code[MAX_CODE_SIZE];
	memset(code, 0, sizeof(code));
	offset = 0;
	int max_offset = 0;
	for (int i = 0; i < total_lines; i++) {
		int new_offset = assemble_line(lines[i], code, offset);
		if (new_offset < 0) {
			fprintf(stderr, "pass 2 failed at line: %s\n", lines[i]);
			fclose(fout);
			return 1;
		}
		offset = new_offset;
		if (offset > max_offset) max_offset = offset;
	}
	fwrite(code, 1, max_offset, fout);
	fclose(fout);
	printf("%d bytes written to %s\n", max_offset, argv[2]);
	return 0;
}
