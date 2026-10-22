/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include "check.h"
#include <string.h>

struct point { int x, y; };
struct rect { struct point a, b; char tag; };
struct big { int v[20]; char name[9]; };
union u { int i; char c[4]; short s; };
typedef struct node { int val; struct node *next; } node;

struct point mkpoint(int x, int y) { struct point p; p.x = x; p.y = y; return p; }
int area(struct rect r) { return (r.b.x - r.a.x) * (r.b.y - r.a.y); }
struct big fill(int k) { struct big b; int i; for (i = 0; i < 20; i++) b.v[i] = i * k; strcpy(b.name, "bigname"); return b; }
void move(struct point *p, int dx) { p->x += dx; }

int main(void) {
	struct point p = {1, 2}, q;
	struct rect r = {{0, 0}, {4, 5}, 'r'};
	struct big b, c;
	union u un;
	node n3 = {3, 0}, n2 = {2, &n3}, n1 = {1, &n2};
	node *it;
	int s = 0;
	struct { int a; char b; } anon = {5, 6};
	CHECK(p.x == 1 && p.y == 2);
	q = p;
	q.x = 10;
	CHECK(p.x == 1 && q.x == 10 && q.y == 2);
	q = mkpoint(7, 8);
	CHECK(q.x == 7 && q.y == 8);
	CHECK(mkpoint(3, 4).y == 4);
	CHECK(area(r) == 20);
	CHECK(r.tag == 'r' && r.b.y == 5);
	r.a = mkpoint(1, 1);
	CHECK(area(r) == 12);
	b = fill(3);
	CHECK(b.v[19] == 57 && strcmp(b.name, "bigname") == 0);
	c = b;
	c.v[0] = 99;
	CHECK(b.v[0] == 0 && c.v[0] == 99 && c.v[5] == 15);
	move(&q, 5);
	CHECK(q.x == 12);
	un.i = 0x11223344;
	CHECK(un.c[0] == 0x44 && un.s == 0x3344);
	CHECK(sizeof(union u) == 4);
	CHECK(sizeof(struct point) == 8);
	CHECK(sizeof(struct rect) == 20);
	CHECK(sizeof(struct big) == 92);
	for (it = &n1; it; it = it->next) s += it->val;
	CHECK(s == 6);
	CHECK(n1.next->next->val == 3);
	CHECK(anon.a == 5 && anon.b == 6);
	{
		struct point arr[3] = {{1, 2}, {3, 4}, {5, 6}};
		struct point *pp = arr;
		CHECK(arr[2].x == 5 && (pp + 1)->y == 4);
		pp++;
		CHECK(pp->x == 3);
	}
	DONE();
}
