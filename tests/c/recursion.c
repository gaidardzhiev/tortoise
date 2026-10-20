#include "check.h"

int fib(int n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }

int ack(int m, int n) {
	if (m == 0) return n + 1;
	if (n == 0) return ack(m - 1, 1);
	return ack(m - 1, ack(m, n - 1));
}

int is_even(int n);
int is_odd(int n) { return n == 0 ? 0 : is_even(n - 1); }
int is_even(int n) { return n == 0 ? 1 : is_odd(n - 1); }

int hanoi(int n, int a, int b, int c) {
	if (n == 0) return 0;
	return hanoi(n - 1, a, c, b) + 1 + hanoi(n - 1, c, b, a);
}

int sum_array(int *a, int n) { return n == 0 ? 0 : a[0] + sum_array(a + 1, n - 1); }

int main(void) {
	int a[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
	CHECK(fib(0) == 0);
	CHECK(fib(1) == 1);
	CHECK(fib(10) == 55);
	CHECK(fib(20) == 6765);
	CHECK(ack(0, 0) == 1);
	CHECK(ack(1, 2) == 4);
	CHECK(ack(2, 3) == 9);
	CHECK(ack(3, 3) == 61);
	CHECK(is_even(10) && !is_even(7) && is_odd(7));
	CHECK(hanoi(10, 1, 2, 3) == 1023);
	CHECK(sum_array(a, 10) == 55);
	DONE();
}
