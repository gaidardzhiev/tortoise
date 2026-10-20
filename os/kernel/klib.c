#include "kernel.h"
#include <stdarg.h>

int memcmp(const void *a, const void *b, unsigned n) {
	const unsigned char *p = a, *q = b;
	for (; n; n--, p++, q++)
		if (*p != *q) return *p - *q;
	return 0;
}

unsigned strlen(const char *s) {
	unsigned n = 0;
	while (s[n]) n++;
	return n;
}

int strcmp(const char *a, const char *b) {
	while (*a && *a == *b) {
		a++;
		b++;
	}
	return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, unsigned n) {
	for (; n; n--, a++, b++) {
		if (*a != *b) return (unsigned char)*a - (unsigned char)*b;
		if (!*a) return 0;
	}
	return 0;
}

char *strncpy(char *d, const char *s, unsigned n) {
	unsigned i = 0;
	for (; i < n && s[i]; i++) d[i] = s[i];
	for (; i < n; i++) d[i] = 0;
	return d;
}

void kputc(int c) {
	wr(UART_DATA, (unsigned)(c & 0xFF));
}

static void kout(int c) {
	kputc(c);
	klog_putc(c);
}

static void kputs(const char *s) {
	while (*s) kout(*s++);
}

static void kputnum(unsigned v, int base, int width, int neg) {
	char buf[12];
	int n = 0;
	do {
		int d = (int)(v % base);
		buf[n++] = (char)(d < 10 ? '0' + d : 'A' + d - 10);
		v /= base;
	} while (v);
	if (neg) kout('-');
	while (n < width) buf[n++] = '0';
	while (n) kout(buf[--n]);
}

void kprintf(const char *f, ...) {
	va_list ap;
	va_start(ap, f);
	for (; *f; f++) {
		int width = 0;
		if (*f != '%') {
			kout(*f);
			continue;
		}
		f++;
		while (*f == '0') f++;
		while (*f >= '0' && *f <= '9') width = width * 10 + *f++ - '0';
		switch (*f) {
		case 'd': {
			int x = va_arg(ap, int);
			if (x < 0) kputnum(-(unsigned)x, 10, width, 1);
			else kputnum((unsigned)x, 10, width, 0);
			break;
		}
		case 'u': kputnum(va_arg(ap, unsigned), 10, width, 0); break;
		case 'x': kputnum(va_arg(ap, unsigned), 16, width, 0); break;
		case 's': kputs(va_arg(ap, char *)); break;
		case 'c': kout(va_arg(ap, int)); break;
		case '%': kout('%'); break;
		default: kout('%'); kout(*f); break;
		}
	}
	va_end(ap);
}

void poweroff(int code) {
	wr(SYS_POWEROFF, (unsigned)code);
	for (;;) ;
}

void panic(const char *msg) {
	kprintf("panic: %s\n", msg);
	poweroff(99);
}
