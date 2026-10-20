#include "check.h"
#include <string.h>

static char comp[10000];

int main(void) {
	int i, j, count = 0, last = 0;
	memset(comp, 0, sizeof(comp));
	for (i = 2; i * i < 10000; i++)
		if (!comp[i])
			for (j = i * i; j < 10000; j += i) comp[j] = 1;
	for (i = 2; i < 10000; i++)
		if (!comp[i]) {
			count++;
			last = i;
		}
	CHECK(count == 1229);
	CHECK(last == 9973);
	CHECK(!comp[7919] && comp[7917]);
	DONE();
}
