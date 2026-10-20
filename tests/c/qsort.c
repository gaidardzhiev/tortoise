#include "check.h"
#include <string.h>

static int icmp(const void *a, const void *b) {
	int x = *(const int *)a, y = *(const int *)b;
	return x < y ? -1 : x > y;
}

static int scmp(const void *a, const void *b) {
	return strcmp(*(char *const *)a, *(char *const *)b);
}

struct rec { int key; char name[8]; };
static int rcmp(const void *a, const void *b) {
	return ((const struct rec *)b)->key - ((const struct rec *)a)->key;
}

int main(void) {
	int a[500], i;
	char *words[] = {"pear", "apple", "fig", "banana", "cherry", "date"};
	struct rec rs[4] = {{3, "c"}, {9, "i"}, {1, "a"}, {5, "e"}};
	srand(7);
	for (i = 0; i < 500; i++) a[i] = rand() % 1000 - 500;
	qsort(a, 500, sizeof(int), icmp);
	for (i = 1; i < 500; i++) CHECK(a[i - 1] <= a[i]);
	qsort(words, 6, sizeof(char *), scmp);
	CHECK(strcmp(words[0], "apple") == 0 && strcmp(words[5], "pear") == 0 && strcmp(words[2], "cherry") == 0);
	qsort(rs, 4, sizeof(struct rec), rcmp);
	CHECK(rs[0].key == 9 && rs[3].key == 1 && strcmp(rs[1].name, "e") == 0);
	for (i = 0; i < 100; i++) a[i] = 100 - i;
	qsort(a, 100, sizeof(int), icmp);
	for (i = 0; i < 100; i++) CHECK(a[i] == i + 1);
	qsort(a, 0, sizeof(int), icmp);
	qsort(a, 1, sizeof(int), icmp);
	DONE();
}
