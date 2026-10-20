/*
 * Copyright (C) 2025 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */
#ifndef DIS_H
#define DIS_H

#include <stdint.h>
#include <stddef.h>

enum {
	F_N,
	F_X,
	F_R8,
	F_I8,
	F_RR,
	F_A16,
	F_RI16,
	F_RD16,
	F_IND,
	F_INDS,
	F_RI32,
	F_RA,
	F_RB,
	F_RI,
	F_REL,
	F_ABS,
	F_M,
	F_MS,
	F_LEAPC,
	F_MFCR,
	F_MTCR,
	F_ADDSP,
	F_COUNT
};

typedef struct {
	const char *name;
	int fmt;
} DisOp;

extern const DisOp dis_ops[256];
extern const int dis_len[F_COUNT];
int dis_format(uint32_t addr, const uint8_t *m, char *out, size_t out_size);

#endif
