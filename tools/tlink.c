/*
 * Copyright (C) 2025 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define NHASH 4096
#define MAXOBJ 512

enum { R_ABS32 = 1, R_REL32 = 2, R_ABS16 = 3 };

typedef struct {
	unsigned name;
	unsigned val;
	int sec;
	int bind;
	unsigned addr;
	int gidx;
} OSym;

typedef struct {
	unsigned off;
	int sec;
	int type;
	unsigned sym;
	unsigned add;
} ORel;

typedef struct {
	char *path;
	unsigned size[4];
	unsigned char *text;
	unsigned char *data;
	OSym *syms;
	int nsyms;
	ORel *rels;
	int nrels;
	char *str;
	unsigned strsz;
	unsigned base[4];
} Obj;

typedef struct {
	char *name;
	int obj;
	int sym;
	unsigned addr;
	int defined;
	int next;
} GSym;

static Obj objs[MAXOBJ];
static int nobjs;
static GSym *gsyms;
static int ngsyms, maxgsyms;
static int ghash[NHASH];
static unsigned char *image;
static unsigned text_addr, text_size, data_addr, data_size, bss_addr, bss_size;

static void die(const char *a, const char *b) {
	fprintf(stderr, "tlink: %s%s\n", a, b ? b : "");
	exit(1);
}

static void *xmalloc(unsigned n) {
	void *p = malloc(n ? n : 1);
	if (!p) die("out of memory", NULL);
	memset(p, 0, n ? n : 1);
	return p;
}

static unsigned rd32(unsigned char *p) {
	return (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}

static void wr32(unsigned char *p, unsigned v) {
	p[0] = v & 0xFF;
	p[1] = (v >> 8) & 0xFF;
	p[2] = (v >> 16) & 0xFF;
	p[3] = (v >> 24) & 0xFF;
}

static unsigned hashs(const char *s) {
	unsigned h = 5381;
	while (*s) h = h * 33 + (unsigned char)*s++;
	return h % NHASH;
}

static int gfind(const char *name) {
	int i = ghash[hashs(name)];
	while (i >= 0) {
		if (strcmp(gsyms[i].name, name) == 0) return i;
		i = gsyms[i].next;
	}
	return -1;
}

static int gget(const char *name) {
	int i = gfind(name);
	unsigned h;
	if (i >= 0) return i;
	if (ngsyms == maxgsyms) {
		maxgsyms = maxgsyms ? maxgsyms * 2 : 1024;
		gsyms = realloc(gsyms, maxgsyms * sizeof(GSym));
		if (!gsyms) die("out of memory", NULL);
	}
	i = ngsyms++;
	memset(&gsyms[i], 0, sizeof(GSym));
	gsyms[i].name = (char *)name;
	gsyms[i].obj = -1;
	h = hashs(name);
	gsyms[i].next = ghash[h];
	ghash[h] = i;
	return i;
}

static void read_obj(const char *path) {
	FILE *f = fopen(path, "rb");
	unsigned char hdr[28];
	unsigned char *b;
	Obj *o;
	int i;
	if (!f) die("cannot open ", path);
	if (nobjs == MAXOBJ) die("too many object files", NULL);
	o = &objs[nobjs++];
	memset(o, 0, sizeof(Obj));
	o->path = (char *)path;
	if (fread(hdr, 1, 28, f) != 28 || memcmp(hdr, "TOF1", 4)) die("not a TOF object: ", path);
	o->size[1] = rd32(hdr + 4);
	o->size[2] = rd32(hdr + 8);
	o->size[3] = rd32(hdr + 12);
	o->nsyms = (int)rd32(hdr + 16);
	o->nrels = (int)rd32(hdr + 20);
	o->strsz = rd32(hdr + 24);
	o->text = xmalloc(o->size[1]);
	o->data = xmalloc(o->size[2]);
	if (fread(o->text, 1, o->size[1], f) != o->size[1]) die("truncated object: ", path);
	if (fread(o->data, 1, o->size[2], f) != o->size[2]) die("truncated object: ", path);
	b = xmalloc(o->nsyms * 12);
	if (fread(b, 1, o->nsyms * 12, f) != (unsigned)o->nsyms * 12) die("truncated object: ", path);
	o->syms = xmalloc(o->nsyms * sizeof(OSym));
	for (i = 0; i < o->nsyms; i++) {
		o->syms[i].name = rd32(b + i * 12);
		o->syms[i].val = rd32(b + i * 12 + 4);
		o->syms[i].sec = b[i * 12 + 8];
		o->syms[i].bind = b[i * 12 + 9];
		o->syms[i].gidx = -1;
	}
	free(b);
	b = xmalloc(o->nrels * 16);
	if (fread(b, 1, o->nrels * 16, f) != (unsigned)o->nrels * 16) die("truncated object: ", path);
	o->rels = xmalloc(o->nrels * sizeof(ORel));
	for (i = 0; i < o->nrels; i++) {
		o->rels[i].off = rd32(b + i * 16);
		o->rels[i].sec = b[i * 16 + 4];
		o->rels[i].type = b[i * 16 + 5];
		o->rels[i].sym = rd32(b + i * 16 + 8);
		o->rels[i].add = rd32(b + i * 16 + 12);
	}
	free(b);
	o->str = xmalloc(o->strsz + 1);
	if (fread(o->str, 1, o->strsz, f) != o->strsz) die("truncated object: ", path);
	fclose(f);
	for (i = 0; i < o->nsyms; i++) {
		if (o->syms[i].name >= o->strsz) die("bad symbol name in ", path);
		if (o->syms[i].sec > 4) die("bad symbol section in ", path);
	}
	for (i = 0; i < o->nrels; i++) {
		unsigned lim = o->rels[i].sec == 1 ? o->size[1] : o->rels[i].sec == 2 ? o->size[2] : 0;
		int w = o->rels[i].type == R_ABS16 ? 2 : 4;
		if (o->rels[i].sec < 1 || o->rels[i].sec > 2 || o->rels[i].off + w > lim) die("bad relocation in ", path);
		if (o->rels[i].sym >= (unsigned)o->nsyms) die("bad relocation symbol in ", path);
		if (o->rels[i].type < 1 || o->rels[i].type > 3) die("bad relocation type in ", path);
	}
}

static unsigned align(unsigned v, unsigned a) {
	return (v + a - 1) & ~(a - 1);
}

static void define_special(const char *name, unsigned addr) {
	int g = gget(name);
	if (gsyms[g].defined) die("linker symbol defined by an object: ", name);
	gsyms[g].defined = 1;
	gsyms[g].addr = addr;
}

int main(int argc, char *argv[]) {
	const char *out = NULL, *entry_name = "_start";
	int flat = 0, explicit_entry = 0, i, j, errors = 0, map = 0;
	unsigned entry, total, pos;
	FILE *f;
	for (i = 0; i < NHASH; i++) ghash[i] = -1;
	text_addr = 0;
	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-b") == 0) flat = 1;
		else if (strcmp(argv[i], "-M") == 0) map = 1;
		else if (strcmp(argv[i], "-T") == 0 && i + 1 < argc) text_addr = (unsigned)strtoul(argv[++i], NULL, 0);
		else if (strcmp(argv[i], "-e") == 0 && i + 1 < argc) {
			entry_name = argv[++i];
			explicit_entry = 1;
		} else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) out = argv[++i];
		else if (argv[i][0] == '-') die("unknown option ", argv[i]);
		else read_obj(argv[i]);
	}
	if (!out || nobjs == 0) {
		fprintf(stderr, "usage: tlink [-b] [-T addr] [-e sym] -o out file.o ...\n");
		return 1;
	}
	pos = text_addr;
	for (i = 0; i < nobjs; i++) {
		pos = align(pos, 4);
		objs[i].base[1] = pos;
		pos += objs[i].size[1];
	}
	text_size = pos - text_addr;
	data_addr = align(pos, 4096);
	pos = data_addr;
	for (i = 0; i < nobjs; i++) {
		pos = align(pos, 4);
		objs[i].base[2] = pos;
		pos += objs[i].size[2];
	}
	data_size = pos - data_addr;
	bss_addr = align(pos, 4);
	pos = bss_addr;
	for (i = 0; i < nobjs; i++) {
		pos = align(pos, 4);
		objs[i].base[3] = pos;
		pos += objs[i].size[3];
	}
	bss_size = pos - bss_addr;
	for (i = 0; i < nobjs; i++) {
		Obj *o = &objs[i];
		for (j = 0; j < o->nsyms; j++) {
			OSym *s = &o->syms[j];
			if (s->sec >= 1 && s->sec <= 3) s->addr = o->base[s->sec] + s->val;
			else s->addr = s->val;
			if (s->bind == 1 && s->sec != 0) {
				int g = gget(o->str + s->name);
				if (gsyms[g].defined) {
					fprintf(stderr, "tlink: duplicate symbol %s in %s and %s\n", gsyms[g].name, gsyms[g].obj >= 0 ? objs[gsyms[g].obj].path : "linker", o->path);
					errors++;
					continue;
				}
				gsyms[g].defined = 1;
				gsyms[g].obj = i;
				gsyms[g].sym = j;
				gsyms[g].addr = s->addr;
			}
		}
	}
	define_special("_etext", text_addr + text_size);
	define_special("_edata", data_addr + data_size);
	define_special("_end", bss_addr + bss_size);
	for (i = 0; i < nobjs; i++) {
		Obj *o = &objs[i];
		for (j = 0; j < o->nsyms; j++) {
			OSym *s = &o->syms[j];
			if (s->sec == 0) {
				int g = gfind(o->str + s->name);
				if (g < 0 || !gsyms[g].defined) {
					int k, used = 0;
					for (k = 0; k < o->nrels; k++)
						if (o->rels[k].sym == (unsigned)j) used = 1;
					if (used) {
						fprintf(stderr, "tlink: undefined symbol %s referenced in %s\n", o->str + s->name, o->path);
						errors++;
					}
					continue;
				}
				s->addr = gsyms[g].addr;
			}
		}
	}
	if (errors) return 1;
	total = data_addr + data_size - text_addr;
	if (data_size == 0) total = text_size;
	image = xmalloc(total);
	for (i = 0; i < nobjs; i++) {
		Obj *o = &objs[i];
		memcpy(image + (o->base[1] - text_addr), o->text, o->size[1]);
		memcpy(image + (o->base[2] - text_addr), o->data, o->size[2]);
	}
	for (i = 0; i < nobjs; i++) {
		Obj *o = &objs[i];
		for (j = 0; j < o->nrels; j++) {
			ORel *r = &o->rels[j];
			unsigned faddr = o->base[r->sec] + r->off;
			unsigned char *p = image + (faddr - text_addr);
			unsigned v = o->syms[r->sym].addr + r->add;
			if (r->type == R_ABS32) {
				wr32(p, v);
			} else if (r->type == R_REL32) {
				wr32(p, v - (faddr + 4));
			} else {
				if (v > 0xFFFF && v < 0xFFFF8000u) {
					fprintf(stderr, "tlink: 16 bit relocation overflow (0x%X) against %s in %s\n", v, o->str + o->syms[r->sym].name, o->path);
					errors++;
				}
				p[0] = v & 0xFF;
				p[1] = (v >> 8) & 0xFF;
			}
		}
	}
	if (errors) return 1;
	entry = text_addr;
	i = gfind(entry_name);
	if (i >= 0 && gsyms[i].defined) entry = gsyms[i].addr;
	else if (explicit_entry) die("entry symbol not found: ", entry_name);
	if (map) {
		for (i = 0; i < nobjs; i++)
			for (j = 0; j < objs[i].nsyms; j++) {
				OSym *s = &objs[i].syms[j];
				if (s->sec >= 1 && s->sec <= 3 && objs[i].str[s->name] && objs[i].str[s->name] != '.') printf("%08x %s %s\n", s->addr, objs[i].str + s->name, objs[i].path);
			}
	}
	f = fopen(out, "wb");
	if (!f) die("cannot create ", out);
	if (flat) {
		fwrite(image, 1, total, f);
	} else {
		unsigned char h[32];
		memcpy(h, "TEX1", 4);
		wr32(h + 4, entry);
		wr32(h + 8, text_addr);
		wr32(h + 12, text_size);
		wr32(h + 16, data_addr);
		wr32(h + 20, data_size);
		wr32(h + 24, bss_size);
		wr32(h + 28, 0);
		fwrite(h, 1, 32, f);
		fwrite(image, 1, text_size, f);
		fwrite(image + (data_addr - text_addr), 1, data_size, f);
	}
	fclose(f);
	chmod(out, 0755);
	return 0;
}
