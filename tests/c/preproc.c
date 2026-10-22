/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include "check.h"
#include "preproc_inc.h"
#include "preproc_inc.h"
#include <string.h>

#define N 10
#define SQ(x) ((x) * (x))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define STR(x) #x
#define XSTR(x) STR(x)
#define CAT(a, b) a##b
#define EMPTY
#define LIST(...) sum(3, __VA_ARGS__)
#define SELF SELF
#define LONG_MACRO(a, b, \
	c) (a + b + c)
#define F(x) G(x) + 1
#define G(x) (x * 2)

static int sum(int n, int a, int b, int c) { return n + a + b + c; }

#if N > 5 && defined(SQ)
static int big = 1;
#else
static int big = 0;
#endif

#ifdef UNDEFINED_THING
#error this must not happen
#elif N == 10
static int ten = 1;
#else
static int ten = 0;
#endif

#ifndef N
#error N missing
#endif

#if 0
this is garbage ' that is skipped
#endif

#define TEMP 5
#undef TEMP
#ifdef TEMP
#error undef failed
#endif

#if (3 * 4 == 12) && !(1 > 2) && (7 % 4 == 3) && ((1 << 4) == 16) && (-1 < 0) && (0x10 == 16) && ('A' == 65)
static int arith_ok = 1;
#else
static int arith_ok = 0;
#endif

int main(void) {
	int CAT(var, 1) = 7;
	int SELF = 3;
	CHECK(N == 10);
	CHECK(SQ(3 + 1) == 16);
	CHECK(MAX(3, 9) == 9 && MAX(-1, -5) == -1);
	CHECK(strcmp(STR(hello world), "hello world") == 0);
	CHECK(strcmp(XSTR(N), "10") == 0);
	CHECK(strcmp(STR("q\"x"), "\"q\\\"x\"") == 0);
	CHECK(var1 == 7);
	CHECK(SELF == 3);
	CHECK(LIST(1, 2, 3) == 9);
	CHECK(LONG_MACRO(1, 2, 3) == 6);
	CHECK(F(5) == 11);
	CHECK(big && ten && arith_ok);
	CHECK(inc_func(1) == 1235);
	CHECK(__LINE__ == 77);
	CHECK(strstr(__FILE__, "preproc.c") != NULL);
	EMPTY;
	DONE();
}
