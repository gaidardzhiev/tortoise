#include <stdio.h>
#include <string.h>

struct greeting {
	const char *who;
	int times;
};

static int square(int x) {
	return x * x;
}

int main(int argc, char **argv) {
	struct greeting g = {"TORTOISE", 2};
	int i;
	for (i = 0; i < g.times; i++) printf("hello, %s %d\n", g.who, square(i + 3));
	printf("argc=%d len=%d\n", argc, (int)strlen(g.who));
	return 0;
}
