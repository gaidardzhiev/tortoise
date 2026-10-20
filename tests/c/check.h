#include <stdio.h>
#include <stdlib.h>
static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)
#define DONE() do { if (failures) { printf("%d failures\n", failures); return 1; } printf("OK\n"); return 0; } while (0)
