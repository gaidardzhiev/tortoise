#include "check.h"

void swap(int *a, int *b) { int t = *a; *a = *b; *b = t; }
void set(int **pp, int *v) { *pp = v; }

int main(void) {
	int a[5] = {10, 20, 30, 40, 50};
	int *p = a, *q = &a[4];
	int x = 1, y = 2;
	int *ptrs[3];
	int **pp;
	char s[] = "hello";
	char *cp = s;
	int m[3][4];
	int i, j;
	short sh[4] = {1, 2, 3, 4};
	short *sp = sh;
	void *vp;
	CHECK(*p == 10);
	CHECK(*(p + 2) == 30);
	CHECK(p[3] == 40);
	CHECK(2[a] == 30);
	CHECK(q - p == 4);
	CHECK(q > p && p < q && p != q);
	p++;
	CHECK(*p == 20);
	p += 2;
	CHECK(*p == 40);
	p--;
	CHECK(*p == 30);
	CHECK(*p++ == 30 && *p == 40);
	CHECK(*--p == 30);
	(*p)++;
	CHECK(a[2] == 31);
	swap(&x, &y);
	CHECK(x == 2 && y == 1);
	ptrs[0] = &x;
	ptrs[1] = &y;
	ptrs[2] = a;
	CHECK(*ptrs[0] == 2 && *ptrs[1] == 1 && ptrs[2][1] == 20);
	pp = &ptrs[1];
	CHECK(**pp == 1);
	**pp = 7;
	CHECK(y == 7);
	set(pp, &x);
	CHECK(ptrs[1] == &x);
	while (*cp) cp++;
	CHECK(cp - s == 5);
	CHECK(sizeof(s) == 6);
	for (i = 0; i < 3; i++)
		for (j = 0; j < 4; j++) m[i][j] = i * 10 + j;
	CHECK(m[2][3] == 23 && m[1][0] == 10);
	CHECK(*(*(m + 1) + 2) == 12);
	CHECK(sizeof(m) == 48 && sizeof(m[0]) == 16);
	CHECK(&m[1][0] - &m[0][0] == 4);
	CHECK(*(sp + 3) == 4);
	sp += 2;
	CHECK(*sp == 3 && sp - sh == 2);
	vp = a;
	CHECK(*(int *)vp == 10);
	CHECK((char *)&a[1] - (char *)&a[0] == 4);
	p = 0;
	CHECK(!p && p == NULL);
	CHECK(&*a == a);
	DONE();
}
