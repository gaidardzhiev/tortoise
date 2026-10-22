/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include "check.h"
#include <string.h>

int gi = 42;
int gz;
char gc = 'x';
short gs = -2;
unsigned gu = 0xDEADBEEF;
int garr[5] = {1, 2, 3};
int gmat[2][3] = {{1, 2, 3}, {4, 5, 6}};
int gflat[2][2] = {1, 2, 3, 4};
char gstr[] = "global";
char gstr2[10] = "ab";
char *gptr = "pointer";
char *gptrs[] = {"one", "two", "three", NULL};
int *gip = &gi;
int *gap = &garr[2];
struct inner { char c; int v; };
struct outer { int id; struct inner in[2]; const char *name; };
struct outer go = {7, {{'a', 1}, {'b', 2}}, "outer"};
struct outer gos[2] = {{1, {{'x', 10}}, "first"}, {2}};
static int sfile = 99;
enum color { RED, GREEN = 5, BLUE };
enum color gcol = BLUE;
int gexpr = (3 + 4) * 2 - 1;
unsigned gsize = sizeof(struct outer);
int (*gfp)(const char *) = 0;
extern int later;
int later = 11;

static int counter(void) {
	static int n;
	static int base = 100;
	n++;
	return base + n;
}

int main(void) {
	CHECK(gi == 42 && gz == 0 && gc == 'x' && gs == -2 && gu == 0xDEADBEEF);
	CHECK(garr[0] == 1 && garr[2] == 3 && garr[3] == 0 && garr[4] == 0);
	CHECK(gmat[1][2] == 6 && gmat[0][1] == 2);
	CHECK(gflat[1][0] == 3);
	CHECK(strcmp(gstr, "global") == 0 && sizeof(gstr) == 7);
	CHECK(strcmp(gstr2, "ab") == 0 && gstr2[9] == 0 && sizeof(gstr2) == 10);
	CHECK(strcmp(gptr, "pointer") == 0);
	CHECK(strcmp(gptrs[2], "three") == 0 && gptrs[3] == NULL);
	CHECK(*gip == 42 && *gap == 3 && gap - garr == 2);
	CHECK(go.id == 7 && go.in[1].c == 'b' && go.in[1].v == 2 && strcmp(go.name, "outer") == 0);
	CHECK(gos[0].in[0].v == 10 && gos[0].in[1].v == 0 && gos[1].id == 2 && gos[1].name == NULL);
	CHECK(sfile == 99);
	CHECK(gcol == 6 && RED == 0 && GREEN == 5);
	CHECK(gexpr == 13);
	CHECK(gsize == 24);
	CHECK(gfp == 0);
	CHECK(later == 11);
	CHECK(counter() == 101 && counter() == 102 && counter() == 103);
	gi = 5;
	CHECK(*gip == 5);
	DONE();
}
