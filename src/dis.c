/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <string.h>
#include "cpu.h"
#include "dis.h"

const DisOp dis_ops[256] = {
	[OP_NOP] = {"NOP", F_N},
	[OP_LOAD] = {"LOAD", F_RI16},
	[OP_ADD] = {"ADD", F_RI16},
	[OP_STORE] = {"STORE", F_RI16},
	[OP_JMP] = {"JMP", F_A16},
	[OP_JZ] = {"JZ", F_RI16},
	[OP_AND] = {"AND", F_RI16},
	[OP_OR] = {"OR", F_RI16},
	[OP_XOR] = {"XOR", F_RI16},
	[OP_NOT] = {"NOT", F_R8},
	[OP_CALL] = {"CALL", F_A16},
	[OP_RET] = {"RET", F_N},
	[OP_IN] = {"IN", F_RI16},
	[OP_OUT] = {"OUT", F_RI16},
	[OP_SUB] = {"SUB", F_RI16},
	[OP_CMP] = {"CMP", F_RI16},
	[OP_JC] = {"JC", F_A16},
	[OP_JN] = {"JN", F_A16},
	[OP_JO] = {"JO", F_A16},
	[OP_JNZ] = {"JNZ", F_A16},
	[OP_SHL] = {"SHL", F_RD16},
	[OP_SHR] = {"SHR", F_RD16},
	[OP_MUL] = {"MUL", F_RD16},
	[OP_DIV] = {"DIV", F_RD16},
	[OP_INT] = {"INT", F_I8},
	[OP_IRET] = {"IRET", F_N},
	[OP_CLI] = {"CLI", F_N},
	[OP_STI] = {"STI", F_N},
	[OP_LOAD32] = {"LOAD32", F_RI32},
	[OP_STORE32] = {"STORE32", F_RI32},
	[OP_ADD32] = {"ADD32", F_RI32},
	[OP_SUB32] = {"SUB32", F_RI32},
	[OP_MUL32] = {"MUL32", F_RI32},
	[OP_DIV32] = {"DIV32", F_RI32},
	[OP_SHL32] = {"SHL32", F_RI32},
	[OP_SHR32] = {"SHR32", F_RI32},
	[OP_AND32] = {"AND32", F_RI32},
	[OP_OR32] = {"OR32", F_RI32},
	[OP_XOR32] = {"XOR32", F_RI32},
	[OP_CMP32] = {"CMP32", F_RI32},
	[OP_MOV] = {"MOV", F_RR},
	[OP_ADD_REG] = {"ADD", F_RR},
	[OP_SUB_REG] = {"SUB", F_RR},
	[OP_CMP_REG] = {"CMP", F_RR},
	[OP_AND_REG] = {"AND", F_RR},
	[OP_OR_REG] = {"OR", F_RR},
	[OP_XOR_REG] = {"XOR", F_RR},
	[OP_MUL_REG] = {"MUL", F_RR},
	[OP_DIV_REG] = {"DIV", F_RR},
	[OP_PUSH] = {"PUSH", F_R8},
	[OP_POP] = {"POP", F_R8},
	[OP_LOAD_INDIR] = {"LOAD", F_IND},
	[OP_STORE_INDIR] = {"STORE", F_INDS},
	[OP_LOAD32_INDIR] = {"LOAD32", F_IND},
	[OP_STORE32_INDIR] = {"STORE32", F_INDS},
	[OP_SYSCALL] = {"SYSCALL", F_N},
	[OP_SYSRET] = {"SYSRET", F_N},
	[OP_JMPR] = {"JMPR", F_RB},
	[OP_CALLR] = {"CALLR", F_RB},
	[OP_JMPREL] = {"JMPREL", F_REL},
	[OP_CALLREL] = {"CALLREL", F_REL},
	[OP_BEQ] = {"BEQ", F_REL},
	[OP_BNE] = {"BNE", F_REL},
	[OP_BLT] = {"BLT", F_REL},
	[OP_BGE] = {"BGE", F_REL},
	[OP_BLE] = {"BLE", F_REL},
	[OP_BGT] = {"BGT", F_REL},
	[OP_BLO] = {"BLO", F_REL},
	[OP_BHS] = {"BHS", F_REL},
	[OP_BLS] = {"BLS", F_REL},
	[OP_BHI] = {"BHI", F_REL},
	[OP_BMI] = {"BMI", F_REL},
	[OP_BPL] = {"BPL", F_REL},
	[OP_BVS] = {"BVS", F_REL},
	[OP_BVC] = {"BVC", F_REL},
	[OP_JMP32] = {"JMP32", F_ABS},
	[OP_CALL32] = {"CALL32", F_ABS},
	[OP_ADC] = {"ADC", F_RR},
	[OP_SBB] = {"SBB", F_RR},
	[OP_SARV] = {"SARV", F_RR},
	[OP_SHLV] = {"SHLV", F_RR},
	[OP_SHRV] = {"SHRV", F_RR},
	[OP_ROLV] = {"ROLV", F_RR},
	[OP_RORV] = {"RORV", F_RR},
	[OP_IMUL] = {"IMUL", F_RR},
	[OP_IDIV] = {"IDIV", F_RR},
	[OP_UMOD] = {"UMOD", F_RR},
	[OP_IMOD] = {"IMOD", F_RR},
	[OP_NEG] = {"NEG", F_RA},
	[OP_TEST] = {"TEST", F_RR},
	[OP_MULHU] = {"MULHU", F_RR},
	[OP_MULHS] = {"MULHS", F_RR},
	[OP_SAR] = {"SAR", F_RI},
	[OP_ROL] = {"ROL", F_RI},
	[OP_ROR] = {"ROR", F_RI},
	[OP_ADCI] = {"ADCI", F_RI},
	[OP_SBBI] = {"SBBI", F_RI},
	[OP_IMULI] = {"IMULI", F_RI},
	[OP_IDIVI] = {"IDIVI", F_RI},
	[OP_UMODI] = {"UMODI", F_RI},
	[OP_IMODI] = {"IMODI", F_RI},
	[OP_TESTI] = {"TESTI", F_RI},
	[OP_LDB] = {"LDB", F_M},
	[OP_LDBS] = {"LDBS", F_M},
	[OP_LDH] = {"LDH", F_M},
	[OP_LDHS] = {"LDHS", F_M},
	[OP_LDW] = {"LDW", F_M},
	[OP_STB] = {"STB", F_MS},
	[OP_STH] = {"STH", F_MS},
	[OP_STW] = {"STW", F_MS},
	[OP_LEA] = {"LEA", F_M},
	[OP_LEAPC] = {"LEAPC", F_LEAPC},
	[OP_MOVSP] = {"MOVSP", F_RA},
	[OP_SETSP] = {"SETSP", F_RB},
	[OP_ADDSP] = {"ADDSP", F_ADDSP},
	[OP_BRK] = {"BRK", F_N},
	[OP_MFCR] = {"MFCR", F_MFCR},
	[OP_MTCR] = {"MTCR", F_MTCR},
	[OP_TLBFLUSH] = {"TLBFLUSH", F_N},
	[OP_INVLPG] = {"INVLPG", F_RB},
	[OP_WFI] = {"WFI", F_N},
	[OP_CPUID] = {"CPUID", F_RA},
	[OP_PUSHF] = {"PUSHF", F_N},
	[OP_POPF] = {"POPF", F_N},
	[OP_RDCYC] = {"RDCYC", F_RA},
	[OP_RET32] = {"RET32", F_N},
	[OP_PUSH32] = {"PUSH32", F_RB},
	[OP_POP32] = {"POP32", F_RA},
	[OP_ADD32R] = {"ADD32", F_RR},
	[OP_SUB32R] = {"SUB32", F_RR},
	[OP_CMP32R] = {"CMP32", F_RR},
	[OP_MUL32R] = {"MUL32", F_RR},
	[OP_DIV32R] = {"DIV32", F_RR},
	[OP_HALT] = {"HALT", F_N},
};

const int dis_len[F_COUNT] = {1, 1, 2, 2, 2, 3, 4, 4, 4, 4, 6, 2, 2, 6, 6, 6, 6, 6, 6, 2, 2, 6};

static void rname(char *b, int r) {
	if (r == 8) strcpy(b, "SP");
	else sprintf(b, "R%d", r);
}

static void memop(char *b, int r, int32_t d) {
	char rn[8];
	rname(rn, r);
	if (d == 0) sprintf(b, "[%s]", rn);
	else if (d < 0) sprintf(b, "[%s - %u]", rn, (unsigned)-(int64_t)d);
	else sprintf(b, "[%s + %d]", rn, d);
}

int dis_format(uint32_t addr, const uint8_t *m, char *out, size_t out_size) {
	uint8_t op = m[0];
	const DisOp *d = &dis_ops[op];
	int a = (m[1] >> 4) & 0xF, b = m[1] & 0xF;
	uint32_t i16 = m[2] | (m[3] << 8);
	uint32_t i32 = m[2] | (m[3] << 8) | (m[4] << 16) | ((uint32_t)m[5] << 24);
	char ra[8], rb[8], mo[32];
	if (!d->name) {
		snprintf(out, out_size, ".byte 0x%02X", op);
		return 1;
	}
	rname(ra, a);
	rname(rb, b);
	switch (d->fmt) {
	case F_N: snprintf(out, out_size, "%s", d->name); break;
	case F_R8: snprintf(out, out_size, "%s R%d", d->name, m[1]); break;
	case F_I8: snprintf(out, out_size, "%s %u", d->name, m[1]); break;
	case F_RR: snprintf(out, out_size, "%s R%d, R%d", d->name, a, b); break;
	case F_A16: snprintf(out, out_size, "%s 0x%04X", d->name, m[1] | (m[2] << 8)); break;
	case F_RI16: snprintf(out, out_size, "%s R%d, 0x%04X", d->name, m[1], i16); break;
	case F_RD16: snprintf(out, out_size, "%s R%d, %u", d->name, m[1], i16); break;
	case F_IND:
		snprintf(out, out_size, "%s R%d, [R%d + %d]", d->name, a, b, (int16_t)i16);
		break;
	case F_INDS:
		snprintf(out, out_size, "%s [R%d + %d], R%d", d->name, a, (int16_t)i16, b);
		break;
	case F_RI32: snprintf(out, out_size, "%s R%d, 0x%08X", d->name, m[1], i32); break;
	case F_RA: snprintf(out, out_size, "%s %s", d->name, ra); break;
	case F_RB: snprintf(out, out_size, "%s %s", d->name, rb); break;
	case F_RI: snprintf(out, out_size, "%s %s, %d", d->name, ra, (int32_t)i32); break;
	case F_REL: snprintf(out, out_size, "%s 0x%08X", d->name, addr + 6 + i32); break;
	case F_ABS: snprintf(out, out_size, "%s 0x%08X", d->name, i32); break;
	case F_ADDSP: snprintf(out, out_size, "%s %d", d->name, (int32_t)i32); break;
	case F_LEAPC: snprintf(out, out_size, "%s %s, 0x%08X", d->name, ra, addr + 6 + i32); break;
	case F_MFCR: snprintf(out, out_size, "%s %s, CR%d", d->name, ra, b); break;
	case F_MTCR: snprintf(out, out_size, "%s CR%d, %s", d->name, a, rb); break;
	case F_M:
		memop(mo, b, (int32_t)i32);
		snprintf(out, out_size, "%s %s, %s", d->name, ra, mo);
		break;
	default:
		memop(mo, b, (int32_t)i32);
		snprintf(out, out_size, "%s %s, %s", d->name, mo, ra);
		break;
	}
	return dis_len[d->fmt];
}

int cpu_disassemble(uint32_t addr, char *out, size_t out_size) {
	uint8_t m[8];
	for (int i = 0; i < 8; i++) {
		if (!mem_peek(addr + i, &m[i])) {
			if (i == 0) {
				snprintf(out, out_size, "??? (out of bounds)");
				return 1;
			}
			m[i] = 0;
		}
	}
	return dis_format(addr, m, out, out_size);
}
