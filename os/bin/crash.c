#include <stdio.h>

int main(void) {
	int *p = (int *)16;
	printf("about to crash\n");
	return *p;
}
