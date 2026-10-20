#include "check.h"

static int add(int a, int b) { return a + b; }
static int sub(int a, int b) { return a - b; }
static int mul(int a, int b) { return a * b; }
typedef int (*binop)(int, int);
static binop table[3] = {add, sub, mul};
struct op { char sym; int (*fn)(int, int); };
static struct op ops[] = {{'+', add}, {'-', sub}, {'*', &mul}};

int apply(int (*f)(int, int), int a, int b) { return f(a, b); }
binop pick(int i) { return table[i]; }
static int counter;
static void inc(void) { counter++; }
void times(void (*f)(void), int n) { while (n--) f(); }

int main(void) {
	int (*fp)(int, int) = add;
	int i;
	CHECK(fp(2, 3) == 5);
	CHECK((*fp)(2, 3) == 5);
	fp = &sub;
	CHECK(fp(2, 3) == -1);
	CHECK(apply(mul, 4, 5) == 20);
	CHECK(table[0](1, 1) == 2 && table[1](1, 1) == 0 && table[2](3, 3) == 9);
	CHECK(pick(2)(6, 7) == 42);
	for (i = 0; i < 3; i++) CHECK(ops[i].fn(10, 2) == (i == 0 ? 12 : i == 1 ? 8 : 20));
	CHECK(ops[2].sym == '*');
	times(inc, 5);
	CHECK(counter == 5);
	DONE();
}
