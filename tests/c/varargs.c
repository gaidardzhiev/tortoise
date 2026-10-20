#include "check.h"
#include <stdarg.h>
#include <string.h>

int sum(int n, ...) {
	va_list ap;
	int s = 0;
	va_start(ap, n);
	while (n--) s += va_arg(ap, int);
	va_end(ap);
	return s;
}

char *cat(char *buf, int n, ...) {
	va_list ap;
	buf[0] = 0;
	va_start(ap, n);
	while (n--) strcat(buf, va_arg(ap, char *));
	va_end(ap);
	return buf;
}

int fmt(char *buf, const char *f, ...) {
	va_list ap;
	int r;
	va_start(ap, f);
	r = vsprintf(buf, f, ap);
	va_end(ap);
	return r;
}

#define EQ(f, ...) do { char b[128]; sprintf(b, f, __VA_ARGS__); if (strcmp(b, want) != 0) { printf("got '%s' want '%s'\n", b, want); failures++; } } while (0)

int main(void) {
	char buf[128];
	char *want;
	CHECK(sum(0) == 0);
	CHECK(sum(3, 1, 2, 3) == 6);
	CHECK(sum(5, -1, -2, 10, 20, 30) == 57);
	CHECK(strcmp(cat(buf, 3, "a", "bc", "def"), "abcdef") == 0);
	CHECK(fmt(buf, "%d-%s", 5, "x") == 3 && strcmp(buf, "5-x") == 0);
	want = "42 -7 0"; EQ("%d %i %d", 42, -7, 0);
	want = "4294967295 ffff FFFF 17"; EQ("%u %x %X %o", 0xFFFFFFFFu, 0xFFFF, 0xFFFF, 15);
	want = "A 'hi' 100%"; EQ("%c '%s' 100%%", 'A', "hi");
	want = "[   42][42   ][00042]"; EQ("[%5d][%-5d][%05d]", 42, 42, 42);
	want = "[  -42][-42  ][-0042]"; EQ("[%5d][%-5d][%05d]", -42, -42, -42);
	want = "[  abc][abc  ]"; EQ("[%5s][%-5s]", "abc", "abc");
	want = "[-2147483648]"; EQ("[%d]", -2147483647 - 1);
	want = "0x1234"; EQ("%p", (void *)0x1234);
	want = "[ff][000000ff]"; EQ("[%x][%08x]", 255, 255);
	want = "ab"; EQ("%.2s", "abcdef");
	CHECK(snprintf(buf, 4, "%d", 123456) == 6 && strcmp(buf, "123") == 0);
	DONE();
}
