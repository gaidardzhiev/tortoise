/*
 * Copyright (C) 2025 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <strings.h>

#define MAX_LINE_LEN 128
#define MAX_CODE_SIZE 65536

//opcodes
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
	OP_HALT = 0xFF
};

//helper to parse register token like "R0" to integer 0
int parse_register(const char* token) {
	if (tolower(token[0]) == 'r' && isdigit(token[1])) {
		int reg = token[1] - '0';
		if (reg >= 0 && reg <= 7) return reg;
	}
	return -1;
}

//helper to parse hex or decimal immediate string to uint16_t
int parse_immediate(const char* token, uint16_t* value) {
	char* endptr = NULL;
	unsigned long val = 0;
	if (strncasecmp(token, "0x", 2) == 0) {
		val = strtoul(token + 2, &endptr, 16);
	} else {
		val = strtoul(token, &endptr, 10);
	}
	if (endptr == token || val > 0xFFFF) return 0;
	*value = (uint16_t)val;
	return 1;
}

//write 16bit value in little endian into buffer at offset
void write_word(uint8_t* buffer, int offset, uint16_t val) {
	buffer[offset] = val & 0xFF;
	buffer[offset + 1] = val >> 8;
}

int assemble_line(const char* line, uint8_t* buffer, int offset) {
	//make a modifiable copy of line
	char line_copy[MAX_LINE_LEN];
	strncpy(line_copy, line, MAX_LINE_LEN);
	line_copy[MAX_LINE_LEN - 1] = '\0';
	//strip comments starting with ';'
	char* comment_pos = strchr(line_copy, ';');
	if (comment_pos) *comment_pos = '\0';
	//trim leading spaces
	char* start = line_copy;
	while (isspace((unsigned char)*start)) start++;
	if (*start == '\0') return offset; //blank or comment line
	//parse instruction and operands (max 2 operands)
	char instr[8] = {0};
	char op1[8] = {0};
	char op2[8] = {0};
	int args = sscanf(start, "%7s %7[^,], %7s", instr, op1, op2);
	if (args < 1) {
		fprintf(stderr, "no instruction found in line: %s\n", line);
		return -1;
	}
	//convert instruction to uppercase
	for (int i = 0; instr[i]; i++) instr[i] = toupper(instr[i]);
	int reg;
	uint16_t val;
	uint16_t addr;
	if (strcmp(instr, "NOP") == 0) {
		buffer[offset++] = OP_NOP;
		return offset;
	} else if (strcmp(instr, "HALT") == 0) {
		buffer[offset++] = OP_HALT;
		return offset;
	} else if (strcmp(instr, "RET") == 0) {
		buffer[offset++] = OP_RET;
		return offset;
	} else if (strcmp(instr, "LOAD") == 0) {
		if (args < 3) {
			fprintf(stderr, "LOAD requires 2 operands\n");
			return -1;
		}
		reg = parse_register(op1);
		if (reg == -1) {
			fprintf(stderr, "invalid register %s\n", op1);
			return -1;
		}
		if (!parse_immediate(op2, &val)) {
			fprintf(stderr, "invalid immediate %s\n", op2);
			return -1;
		}
		buffer[offset++] = OP_LOAD;
		buffer[offset++] = (uint8_t)reg;
		write_word(buffer, offset, val);
		offset += 2;
		return offset;
	} else if (strcmp(instr, "ADD") == 0) {
		if (args < 3) {
			fprintf(stderr, "ADD requires 2 operands\n");
			return -1;
		}
		reg = parse_register(op1);
		if (reg == -1) {
			fprintf(stderr, "invalid register %s\n", op1);
			return -1;
		}
		if (!parse_immediate(op2, &val)) {
			fprintf(stderr, "invalid immediate %s\n", op2);
			return -1;
		}
		buffer[offset++] = OP_ADD;
		buffer[offset++] = (uint8_t)reg;
		write_word(buffer, offset, val);
		offset += 2;
		return offset;
	} else if (strcmp(instr, "STORE") == 0) {
		if (args < 3) {
			fprintf(stderr, "STORE requires 2 operands\n");
			return -1;
		}
		reg = parse_register(op1);
		if (reg == -1) {
			fprintf(stderr, "invalid register %s\n", op1);
			return -1;
		}
		if (!parse_immediate(op2, &addr)) {
			fprintf(stderr, "invalid address %s\n", op2);
			return -1;
		}
		buffer[offset++] = OP_STORE;
		buffer[offset++] = (uint8_t)reg;
		write_word(buffer, offset, addr);
		offset += 2;
		return offset;
	} else if (strcmp(instr, "JMP") == 0) {
		if (args < 2) {
			fprintf(stderr, "JMP requires 1 operand\n");
			return -1;
		}
		if (!parse_immediate(op1, &addr)) {
			fprintf(stderr, "invalid address %s\n", op1);
			return -1;
		}
		buffer[offset++] = OP_JMP;
		write_word(buffer, offset, addr);
		offset += 2;
		return offset;
	} else if (strcmp(instr, "JZ") == 0) {
		if (args < 3) {
			fprintf(stderr, "JZ requires 2 operands\n");
			return -1;
		}
		reg = parse_register(op1);
		if (reg == -1) {
			fprintf(stderr, "invalid register %s\n", op1);
			return -1;
		}
		if (!parse_immediate(op2, &addr)) {
			fprintf(stderr, "invalid address %s\n", op2);
			return -1;
		}
		buffer[offset++] = OP_JZ;
		buffer[offset++] = (uint8_t)reg;
		write_word(buffer, offset, addr);
		offset += 2;
		return offset;
	} else if (strcmp(instr, "AND") == 0) {
		if (args < 3) {
			fprintf(stderr, "AND requires 2 operands\n");
			return -1;
		}
		reg = parse_register(op1);
		if (reg == -1) {
			fprintf(stderr, "invalid register %s\n", op1);
			return -1;
		}
		if (!parse_immediate(op2, &val)) {
			fprintf(stderr, "invalid immediate %s\n", op2);
			return -1;
		}
		buffer[offset++] = OP_AND;
		buffer[offset++] = (uint8_t)reg;
		write_word(buffer, offset, val);
		offset += 2;
		return offset;
	} else if (strcmp(instr, "OR") == 0) {
		if (args < 3) {
			fprintf(stderr, "OR requires 2 operands\n");
			return -1;
		}
		reg = parse_register(op1);
		if (reg == -1) {
			fprintf(stderr, "invalid register %s\n", op1);
			return -1;
		}
		if (!parse_immediate(op2, &val)) {
			fprintf(stderr, "invalid immediate %s\n", op2);
			return -1;
		}
		buffer[offset++] = OP_OR;
		buffer[offset++] = (uint8_t)reg;
		write_word(buffer, offset, val);
		offset += 2;
		return offset;
	} else if (strcmp(instr, "XOR") == 0) {
		if (args < 3) {
			fprintf(stderr, "XOR requires 2 operands\n");
			return -1;
		}
		reg = parse_register(op1);
		if (reg == -1) {
			fprintf(stderr, "invalid register %s\n", op1);
			return -1;
		}
		if (!parse_immediate(op2, &val)) {
			fprintf(stderr, "invalid immediate %s\n", op2);
			return -1;
		}
		buffer[offset++] = OP_XOR;
		buffer[offset++] = (uint8_t)reg;
		write_word(buffer, offset, val);
		offset += 2;
		return offset;
	} else if (strcmp(instr, "NOT") == 0) {
		if (args < 2) {
			fprintf(stderr, "NOT requires 1 operand\n");
			return -1;
		}
		reg = parse_register(op1);
		if (reg == -1) {
			fprintf(stderr, "invalid register %s\n", op1);
			return -1;
		}
		buffer[offset++] = OP_NOT;
		buffer[offset++] = (uint8_t)reg;
		return offset;
	} else if (strcmp(instr, "CALL") == 0) {
		if (args < 2) {
			fprintf(stderr, "CALL requires 1 operand\n");
			return -1;
		}
		if (!parse_immediate(op1, &addr)) {
			fprintf(stderr, "invalid address %s\n", op1);
			return -1;
		}
		buffer[offset++] = OP_CALL;
		write_word(buffer, offset, addr);
		offset += 2;
		return offset;
	} else if (strcmp(instr, "IN") == 0) {
		if (args < 3) {
			fprintf(stderr, "IN requires 2 operands\n");
			return -1;
		}
		reg = parse_register(op1);
		if (reg == -1) {
			fprintf(stderr, "invalid register %s\n", op1);
			return -1;
		}
		if (!parse_immediate(op2, &val)) {
			fprintf(stderr, "invalid immediate %s\n", op2);
			return -1;
		}
		buffer[offset++] = OP_IN;
		buffer[offset++] = (uint8_t)reg;
		write_word(buffer, offset, val);
		offset += 2;
		return offset;
	} else if (strcmp(instr, "OUT") == 0) {
		if (args < 3) {
			fprintf(stderr, "OUT requires 2 operands\n");
			return -1;
		}
		reg = parse_register(op1);
		if (reg == -1) {
			fprintf(stderr, "invalid register %s\n", op1);
			return -1;
		}
		if (!parse_immediate(op2, &val)) {
			fprintf(stderr, "invalid immediate %s\n", op2);
			return -1;
		}
		buffer[offset++] = OP_OUT;
		buffer[offset++] = (uint8_t)reg;
		write_word(buffer, offset, val);
		offset += 2;
		return offset;
	} else {
		fprintf(stderr, "unknown or malformed instruction: %s\n", line);
		return -1;
	}
}

int main(int argc, char* argv[]) {
	if (argc != 3) {
		fprintf(stderr, "usage: %s input.asm output.bin\n", argv[0]);
		return 1;
	}
	FILE* fin = fopen(argv[1], "r");
	if (!fin) {
		perror("fopen input");
		return 1;
	}
	FILE* fout = fopen(argv[2], "wb");
	if (!fout) {
		perror("fopen output");
		fclose(fin);
		return 1;
	}
	uint8_t code[MAX_CODE_SIZE];
	int offset = 0;
	char line[MAX_LINE_LEN];
	while (fgets(line, sizeof(line), fin)) {
		//skip comment or empty lines
		char* p = line;
		while (isspace((unsigned char)*p)) p++;
		if (*p == ';' || *p == '\0' || *p == '\n') continue;
		int new_offset = assemble_line(line, code, offset);
		if (new_offset < 0) {
			fprintf(stderr, "failed at line: %s\n", line);
			fclose(fin);
			fclose(fout);
			return 1;
		}
		offset = new_offset;
	}
	fwrite(code, 1, offset, fout);
	fclose(fin);
	fclose(fout);
	printf("%d bytes written to %s\n", offset, argv[2]);
	return 0;
}
