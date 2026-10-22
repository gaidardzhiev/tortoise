/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include "kernel.h"

#define NCLASS 8
#define NTIMER 128
#define KLOG 16384

typedef struct FreeObj {
	struct FreeObj *next;
} FreeObj;

typedef struct {
	unsigned expire;
	void (*fn)(void *);
	void *arg;
	int used;
} KTimer;

static FreeObj *freelist[NCLASS];
static unsigned char pclass[LOWMEM / PAGE];
static unsigned short plen[LOWMEM / PAGE];
static KTimer ktimers[NTIMER];
static char klog_buf[KLOG];
static unsigned klog_len;

void *kmalloc(unsigned n) {
	int c = 0;
	unsigned sz = 16;
	FreeObj *o;
	if (n == 0) n = 1;
	if (n > 2048) {
		int np = (int)((n + PAGE - 1) / PAGE);
		unsigned pa = low_alloc_n(np);
		if (!pa) return NULL;
		pclass[pa / PAGE] = 0xFF;
		plen[pa / PAGE] = (unsigned short)np;
		return (void *)pa;
	}
	while (sz < n) {
		sz <<= 1;
		c++;
	}
	if (!freelist[c]) {
		unsigned pa = low_alloc(), off;
		if (!pa) return NULL;
		pclass[pa / PAGE] = (unsigned char)(c + 1);
		for (off = 0; off + sz <= PAGE; off += sz) {
			o = (FreeObj *)(pa + off);
			o->next = freelist[c];
			freelist[c] = o;
		}
	}
	o = freelist[c];
	freelist[c] = o->next;
	memset(o, 0, sz);
	return o;
}

void kfree(void *p) {
	unsigned pg;
	int c;
	if (!p) return;
	pg = (unsigned)p / PAGE;
	c = pclass[pg];
	if (c == 0xFF) {
		pclass[pg] = 0;
		low_free_n((unsigned)p, plen[pg]);
		return;
	}
	if (c < 1 || c > NCLASS) panic("kfree: bad pointer");
	((FreeObj *)p)->next = freelist[c - 1];
	freelist[c - 1] = (FreeObj *)p;
}

void kmalloc_selftest(void) {
	static void *ptrs[400];
	unsigned after1 = 0, i, k, round;
	for (round = 0; round < 8; round++) {
		if (round == 4) after1 = low_free_pages();
		for (i = 0; i < 400; i++) {
			unsigned sz = (i * 37 + round * 101) % 3000 + 1;
			ptrs[i] = kmalloc(sz);
			if (!ptrs[i]) panic("kmalloc selftest: out of memory");
			memset(ptrs[i], (int)(i & 0xFF), sz < 16 ? sz : 16);
		}
		for (i = 0; i < 400; i++) {
			unsigned char *b = ptrs[i];
			for (k = 0; k < 1; k++)
				if (b[k] != (unsigned char)(i & 0xFF)) panic("kmalloc selftest: corrupted object");
		}
		for (i = 0; i < 400; i += 2) kfree(ptrs[i]);
		for (i = 1; i < 400; i += 2) kfree(ptrs[i]);
	}
	if (low_free_pages() != after1) panic("kmalloc selftest: pages leaked");
	kprintf("kmalloc selftest ok, 3200 allocations of 1 to 3000 bytes, no pages leaked\n");
}

int timer_add(unsigned n, void (*fn)(void *), void *arg) {
	int i;
	for (i = 0; i < NTIMER; i++)
		if (!ktimers[i].used) {
			ktimers[i].used = 1;
			ktimers[i].expire = ticks + n;
			ktimers[i].fn = fn;
			ktimers[i].arg = arg;
			return i + 1;
		}
	return 0;
}

void timer_cancel(int id) {
	if (id > 0 && id <= NTIMER) ktimers[id - 1].used = 0;
}

void timer_tick(void) {
	int i;
	for (i = 0; i < NTIMER; i++)
		if (ktimers[i].used && (int)(ticks - ktimers[i].expire) >= 0) {
			ktimers[i].used = 0;
			ktimers[i].fn(ktimers[i].arg);
		}
}

void klog_putc(int c) {
	klog_buf[klog_len % KLOG] = (char)c;
	klog_len++;
}

int klog_read(char *dst, unsigned off, unsigned n) {
	unsigned start = klog_len > KLOG ? klog_len - KLOG : 0, i;
	if (off < start) off = start;
	for (i = 0; i < n && off + i < klog_len; i++) dst[i] = klog_buf[(off + i) % KLOG];
	return (int)i;
}
