/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define BS 512
#define FS0 1024
#define NINODES 8192
#define NIND (BS / 4)

static FILE *img;
static int ver;
static unsigned total, ninodes, istart, bmstart, dstart, free_blocks, free_inodes;
static unsigned isize, ipb;

static void die(const char *a, const char *b) {
	fprintf(stderr, "tdisk: %s%s\n", a, b ? b : "");
	exit(1);
}

static unsigned g32(const unsigned char *p) {
	return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24);
}

static unsigned g16(const unsigned char *p) {
	return p[0] | (p[1] << 8);
}

static void p32(unsigned char *p, unsigned v) {
	p[0] = v & 0xFF;
	p[1] = (v >> 8) & 0xFF;
	p[2] = (v >> 16) & 0xFF;
	p[3] = (v >> 24) & 0xFF;
}

static void p16(unsigned char *p, unsigned v) {
	p[0] = v & 0xFF;
	p[1] = (v >> 8) & 0xFF;
}

static void rsec(unsigned s, void *buf) {
	if (fseek(img, (long)s * BS, SEEK_SET) || fread(buf, 1, BS, img) != BS) die("read error", NULL);
}

static void wsec(unsigned s, const void *buf) {
	if (fseek(img, (long)s * BS, SEEK_SET) || fwrite(buf, 1, BS, img) != BS) die("write error", NULL);
}

static void rblk(unsigned b, void *buf) {
	rsec(FS0 + b, buf);
}

static void wblk(unsigned b, const void *buf) {
	wsec(FS0 + b, buf);
}

typedef struct {
	unsigned type, nlink, mode, uid, gid, size, atime, mtime, ctime, direct[12], ind, dind, tind;
} Ino;

static void rino(unsigned n, Ino *ip) {
	unsigned char b[BS], *p;
	int i;
	rblk(istart + n / ipb, b);
	p = b + (n % ipb) * isize;
	memset(ip, 0, sizeof(*ip));
	ip->type = g16(p);
	ip->nlink = g16(p + 2);
	if (ver == 1) {
		ip->size = g32(p + 4);
		for (i = 0; i < 12; i++) ip->direct[i] = g32(p + 8 + i * 4);
		ip->ind = g32(p + 56);
		ip->dind = g32(p + 60);
		ip->mode = ip->type == 2 ? 0755 : 0644;
		return;
	}
	ip->mode = g16(p + 4);
	ip->uid = g16(p + 6);
	ip->gid = g16(p + 8);
	ip->size = g32(p + 12);
	ip->atime = g32(p + 16);
	ip->mtime = g32(p + 20);
	ip->ctime = g32(p + 24);
	for (i = 0; i < 12; i++) ip->direct[i] = g32(p + 28 + i * 4);
	ip->ind = g32(p + 76);
	ip->dind = g32(p + 80);
	ip->tind = g32(p + 84);
}

static void wino(unsigned n, Ino *ip) {
	unsigned char b[BS], *p;
	int i;
	rblk(istart + n / ipb, b);
	p = b + (n % ipb) * isize;
	memset(p, 0, isize);
	p16(p, ip->type);
	p16(p + 2, ip->nlink);
	if (ver == 1) {
		p32(p + 4, ip->size);
		for (i = 0; i < 12; i++) p32(p + 8 + i * 4, ip->direct[i]);
		p32(p + 56, ip->ind);
		p32(p + 60, ip->dind);
		wblk(istart + n / ipb, b);
		return;
	}
	p16(p + 4, ip->mode);
	p16(p + 6, ip->uid);
	p16(p + 8, ip->gid);
	p32(p + 12, ip->size);
	p32(p + 16, ip->atime);
	p32(p + 20, ip->mtime);
	p32(p + 24, ip->ctime);
	for (i = 0; i < 12; i++) p32(p + 28 + i * 4, ip->direct[i]);
	p32(p + 76, ip->ind);
	p32(p + 80, ip->dind);
	p32(p + 84, ip->tind);
	wblk(istart + n / ipb, b);
}

static unsigned balloc(void) {
	unsigned char b[BS], z[BS];
	unsigned i, nb = (total + BS * 8 - 1) / (BS * 8);
	for (i = 0; i < nb; i++) {
		unsigned j;
		rblk(bmstart + i, b);
		for (j = 0; j < BS * 8; j++) {
			unsigned blk = i * BS * 8 + j;
			if (blk >= total) die("disk full", NULL);
			if (!(b[j / 8] & (1 << (j % 8)))) {
				b[j / 8] |= 1 << (j % 8);
				wblk(bmstart + i, b);
				memset(z, 0, BS);
				wblk(blk, z);
				free_blocks--;
				return blk;
			}
		}
	}
	die("disk full", NULL);
	return 0;
}

static unsigned ind(unsigned *slot, unsigned idx, int alloc) {
	unsigned char b[BS];
	unsigned v;
	if (!*slot) {
		if (!alloc) return 0;
		*slot = balloc();
	}
	rblk(*slot, b);
	v = g32(b + idx * 4);
	if (!v && alloc) {
		v = balloc();
		rblk(*slot, b);
		p32(b + idx * 4, v);
		wblk(*slot, b);
	}
	return v;
}

static unsigned bmap(Ino *ip, unsigned fb, int alloc) {
	if (fb < 12) {
		if (!ip->direct[fb] && alloc) ip->direct[fb] = balloc();
		return ip->direct[fb];
	}
	fb -= 12;
	if (fb < NIND) return ind(&ip->ind, fb, alloc);
	fb -= NIND;
	if (fb < NIND * NIND) {
		unsigned mid = ind(&ip->dind, fb / NIND, alloc);
		if (!mid) return 0;
		return ind(&mid, fb % NIND, alloc);
	}
	fb -= NIND * NIND;
	if (ver == 2 && fb < NIND * NIND * NIND) {
		unsigned a = ind(&ip->tind, fb / (NIND * NIND), alloc), b2;
		if (!a) return 0;
		b2 = ind(&a, (fb / NIND) % NIND, alloc);
		if (!b2) return 0;
		return ind(&b2, fb % NIND, alloc);
	}
	die("file too large", NULL);
	return 0;
}

static void iwrite(unsigned n, const unsigned char *data, unsigned off, unsigned len) {
	Ino ip;
	unsigned char b[BS];
	rino(n, &ip);
	while (len) {
		unsigned blk = bmap(&ip, off / BS, 1), o = off % BS, k = BS - o;
		if (k > len) k = len;
		rblk(blk, b);
		memcpy(b + o, data, k);
		wblk(blk, b);
		data += k;
		off += k;
		len -= k;
	}
	if (off > ip.size) ip.size = off;
	wino(n, &ip);
}

static void iread(unsigned n, unsigned char *data, unsigned off, unsigned len) {
	Ino ip;
	unsigned char b[BS];
	rino(n, &ip);
	while (len) {
		unsigned blk = bmap(&ip, off / BS, 0), o = off % BS, k = BS - o;
		if (k > len) k = len;
		if (blk) {
			rblk(blk, b);
			memcpy(data, b + o, k);
		} else {
			memset(data, 0, k);
		}
		data += k;
		off += k;
		len -= k;
	}
}

static unsigned ialloc(unsigned type, unsigned mode, unsigned uid, unsigned gid) {
	unsigned n;
	for (n = 1; n < ninodes; n++) {
		Ino ip;
		rino(n, &ip);
		if (ip.type == 0) {
			memset(&ip, 0, sizeof(ip));
			ip.type = type;
			ip.nlink = 1;
			ip.mode = mode & 07777;
			ip.uid = uid;
			ip.gid = gid;
			wino(n, &ip);
			free_inodes--;
			return n;
		}
	}
	die("out of inodes", NULL);
	return 0;
}

static unsigned lookup(unsigned dir, const char *name) {
	Ino ip;
	unsigned off;
	unsigned char e[32];
	rino(dir, &ip);
	for (off = 0; off < ip.size; off += 32) {
		iread(dir, e, off, 32);
		if (g32(e) && strncmp((char *)e + 4, name, 27) == 0) return g32(e);
	}
	return 0;
}

static void link_name(unsigned dir, const char *name, unsigned n) {
	Ino ip;
	unsigned off;
	unsigned char e[32];
	if (strlen(name) > 27) die("name too long: ", name);
	rino(dir, &ip);
	for (off = 0; off < ip.size; off += 32) {
		iread(dir, e, off, 32);
		if (!g32(e)) break;
	}
	memset(e, 0, 32);
	p32(e, n);
	strncpy((char *)e + 4, name, 27);
	iwrite(dir, e, off, 32);
}

static unsigned mkdir_in(unsigned parent, const char *name, unsigned mode, unsigned uid, unsigned gid) {
	unsigned n = ialloc(2, mode, uid, gid);
	Ino ip;
	link_name(n, ".", n);
	link_name(n, "..", parent ? parent : n);
	rino(n, &ip);
	ip.nlink = 2;
	wino(n, &ip);
	if (name) {
		link_name(parent, name, n);
		rino(parent, &ip);
		ip.nlink++;
		wino(parent, &ip);
	}
	return n;
}

static unsigned resolve(const char *path, int create_dirs, char *last) {
	unsigned cur = 1;
	char comp[64];
	const char *p = path;
	while (*p == '/') p++;
	while (*p) {
		int k = 0;
		unsigned next;
		while (*p && *p != '/') {
			if (k < 63) comp[k++] = *p;
			p++;
		}
		comp[k] = 0;
		while (*p == '/') p++;
		if (!*p && last) {
			strcpy(last, comp);
			return cur;
		}
		next = lookup(cur, comp);
		if (!next) {
			if (!create_dirs) return 0;
			next = mkdir_in(cur, comp, 0755, 0, 0);
		}
		cur = next;
	}
	if (last) last[0] = 0;
	return cur;
}

static void open_sb(void) {
	unsigned char b[BS];
	rblk(0, b);
	if (!memcmp(b, "TFS1", 4)) {
		ver = 1;
		isize = 64;
	} else if (!memcmp(b, "TFS2", 4)) {
		ver = 2;
		isize = 128;
	} else {
		die("no TFS file system in image", NULL);
	}
	ipb = BS / isize;
	total = g32(b + 4);
	ninodes = g32(b + 8);
	istart = g32(b + 12);
	bmstart = g32(b + 16);
	dstart = g32(b + 20);
	free_blocks = g32(b + 24);
	free_inodes = g32(b + 28);
}

static void write_sb(void) {
	unsigned char b[BS];
	memset(b, 0, BS);
	memcpy(b, ver == 1 ? "TFS1" : "TFS2", 4);
	p32(b + 4, total);
	p32(b + 8, ninodes);
	p32(b + 12, istart);
	p32(b + 16, bmstart);
	p32(b + 20, dstart);
	p32(b + 24, free_blocks);
	p32(b + 28, free_inodes);
	wblk(0, b);
}

static unsigned char *slurp(const char *path, unsigned *len) {
	FILE *f = fopen(path, "rb");
	unsigned char *buf;
	long n;
	if (!f) die("cannot open ", path);
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	fseek(f, 0, SEEK_SET);
	buf = malloc(n + 1);
	if (!buf || fread(buf, 1, n, f) != (size_t)n) die("cannot read ", path);
	fclose(f);
	*len = (unsigned)n;
	return buf;
}

static void parse_attr(char *s, unsigned *mode, unsigned *uid, unsigned *gid) {
	char *c;
	if (!s) return;
	c = strchr(s, ':');
	if (c) *c = 0;
	if (*s) *mode = (unsigned)strtoul(s, NULL, 8);
	if (!c) return;
	s = c + 1;
	c = strchr(s, ':');
	if (!c) die("expected uid:gid in ", s);
	*c = 0;
	*uid = (unsigned)strtoul(s, NULL, 10);
	*gid = (unsigned)strtoul(c + 1, NULL, 10);
}

static void add_file(char *arg) {
	char last[64], *fspath, *attr;
	unsigned len, dir, n, mode = 0644, uid = 0, gid = 0;
	unsigned char *data;
	struct stat hs;
	fspath = strchr(arg, ':');
	if (!fspath) die("expected hostpath:/fs/path, got ", arg);
	*fspath++ = 0;
	attr = strchr(fspath, ':');
	if (attr) *attr++ = 0;
	if (stat(arg, &hs) == 0 && (hs.st_mode & 0111)) mode = 0755;
	parse_attr(attr, &mode, &uid, &gid);
	data = slurp(arg, &len);
	dir = resolve(fspath, 1, last);
	if (!last[0]) die("bad file system path ", fspath);
	if (lookup(dir, last)) die("file exists: ", fspath);
	n = ialloc(1, mode, uid, gid);
	link_name(dir, last, n);
	iwrite(n, data, 0, len);
	free(data);
}

static void add_dir(char *arg) {
	char last[64], *attr = strchr(arg, ':');
	unsigned mode = 0755, uid = 0, gid = 0, dir, n;
	Ino ip;
	if (attr) *attr++ = 0;
	parse_attr(attr, &mode, &uid, &gid);
	dir = resolve(arg, 1, last);
	if (!last[0]) die("bad directory path ", arg);
	n = lookup(dir, last);
	if (!n) {
		mkdir_in(dir, last, mode, uid, gid);
		return;
	}
	rino(n, &ip);
	if (ip.type != 2) die("not a directory: ", arg);
	ip.mode = mode;
	ip.uid = uid;
	ip.gid = gid;
	wino(n, &ip);
}

static void add_link(char *arg) {
	char last[64], *path = strchr(arg, ':');
	unsigned dir, n;
	if (!path) die("expected target:/fs/path, got ", arg);
	*path++ = 0;
	dir = resolve(path, 1, last);
	if (!last[0]) die("bad link path ", path);
	if (lookup(dir, last)) die("file exists: ", path);
	n = ialloc(3, 0777, 0, 0);
	link_name(dir, last, n);
	iwrite(n, (unsigned char *)arg, 0, strlen(arg));
}

static int want_v1;

static void create(const char *out, unsigned sectors, const char *boot, const char *kernel) {
	unsigned char b[BS];
	unsigned len, i, bmblocks;
	unsigned char *data;
	img = fopen(out, "w+b");
	if (!img) die("cannot create ", out);
	if (sectors < FS0 + 1 + NINODES / 4 + 64) die("image too small", NULL);
	if (fseek(img, (long)sectors * BS - 1, SEEK_SET) || fputc(0, img) == EOF) die("cannot size image", NULL);
	if (boot) {
		memset(b, 0, BS);
		data = slurp(boot, &len);
		if (len > BS) die("boot sector larger than 512 bytes", NULL);
		memcpy(b, data, len);
		wsec(0, b);
		free(data);
	}
	if (kernel) {
		data = slurp(kernel, &len);
		if (len > (FS0 - 1) * BS) die("kernel too large", NULL);
		for (i = 0; i * BS < len; i++) {
			memset(b, 0, BS);
			memcpy(b, data + i * BS, len - i * BS < BS ? len - i * BS : BS);
			wsec(1 + i, b);
		}
		free(data);
	}
	ver = want_v1 ? 1 : 2;
	isize = want_v1 ? 64 : 128;
	ipb = BS / isize;
	total = sectors - FS0;
	ninodes = want_v1 ? 4096 : NINODES;
	istart = 1;
	bmstart = istart + ninodes / ipb;
	bmblocks = (total + BS * 8 - 1) / (BS * 8);
	dstart = bmstart + bmblocks;
	free_blocks = total - dstart;
	free_inodes = ninodes - 1;
	memset(b, 0, BS);
	for (i = istart; i < dstart; i++) wblk(i, b);
	for (i = 0; i < dstart; i++) {
		unsigned char bm[BS];
		rblk(bmstart + i / (BS * 8), bm);
		bm[(i % (BS * 8)) / 8] |= 1 << (i % 8);
		wblk(bmstart + i / (BS * 8), bm);
	}
	write_sb();
	if (mkdir_in(0, NULL, 0755, 0, 0) != 1) die("root inode is not 1", NULL);
}

static int longfmt;

static void list_dir(unsigned dir, const char *prefix) {
	Ino ip;
	unsigned off;
	unsigned char e[32];
	rino(dir, &ip);
	for (off = 0; off < ip.size; off += 32) {
		char name[29], path[512];
		Ino c;
		unsigned n;
		iread(dir, e, off, 32);
		n = g32(e);
		if (!n) continue;
		memcpy(name, e + 4, 28);
		name[28] = 0;
		if (!strcmp(name, ".") || !strcmp(name, "..")) continue;
		snprintf(path, sizeof(path), "%s/%s", prefix, name);
		rino(n, &c);
		if (longfmt) printf("%04o %4u %4u %2u ", c.mode, c.uid, c.gid, c.nlink);
		if (c.type == 2) {
			printf("%8s  %s/\n", "dir", path);
			list_dir(n, path);
		} else if (c.type == 3) {
			char t[256];
			unsigned l = c.size < 255 ? c.size : 255;
			iread(n, (unsigned char *)t, 0, l);
			t[l] = 0;
			printf("%8s  %s -> %s\n", "link", path, t);
		} else {
			printf("%8u  %s\n", c.size, path);
		}
	}
}

int main(int argc, char **argv) {
	const char *out = NULL, *boot = NULL, *kernel = NULL;
	unsigned sectors = 131072;
	int i;
	if (argc >= 3 && (strcmp(argv[1], "-l") == 0 || strcmp(argv[1], "-ll") == 0)) {
		longfmt = argv[1][2] == 'l';
		img = fopen(argv[2], "rb");
		if (!img) die("cannot open ", argv[2]);
		open_sb();
		list_dir(1, "");
		return 0;
	}
	if (argc == 5 && strcmp(argv[1], "-x") == 0) {
		unsigned n, len;
		Ino ip;
		unsigned char *buf;
		FILE *f;
		img = fopen(argv[2], "rb");
		if (!img) die("cannot open ", argv[2]);
		open_sb();
		n = resolve(argv[3], 0, NULL);
		if (!n) die("no such file: ", argv[3]);
		rino(n, &ip);
		if (ip.type != 1) die("not a regular file: ", argv[3]);
		len = ip.size;
		buf = malloc(len + 1);
		iread(n, buf, 0, len);
		f = fopen(argv[4], "wb");
		if (!f || fwrite(buf, 1, len, f) != len) die("cannot write ", argv[4]);
		fclose(f);
		return 0;
	}
	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) out = argv[++i];
		else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) sectors = (unsigned)strtoul(argv[++i], NULL, 0);
		else if (strcmp(argv[i], "-b") == 0 && i + 1 < argc) boot = argv[++i];
		else if (strcmp(argv[i], "-k") == 0 && i + 1 < argc) kernel = argv[++i];
		else if ((strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "-L") == 0) && i + 1 < argc) i++;
		else if (strcmp(argv[i], "-1") == 0) want_v1 = 1;
		else if (argv[i][0] == '-') die("unknown option ", argv[i]);
	}
	if (!out) {
		fprintf(stderr, "usage: tdisk -o disk.img [-s sectors] [-b boot.bin] [-k kernel.tex] [-d /dir[:mode[:uid:gid]]] [-L target:/path] [hostpath:/fs/path[:mode[:uid:gid]] ...]\n       tdisk -l disk.img\n       tdisk -x disk.img /fs/path hostpath\n");
		return 1;
	}
	create(out, sectors, boot, kernel);
	for (i = 1; i < argc; i++) {
		if ((strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "-b") == 0 || strcmp(argv[i], "-k") == 0) && i + 1 < argc) i++;
		else if (strcmp(argv[i], "-1") == 0) ;
		else if (strcmp(argv[i], "-d") == 0) add_dir(argv[++i]);
		else if (strcmp(argv[i], "-L") == 0) add_link(argv[++i]);
		else add_file(argv[i]);
	}
	write_sb();
	fclose(img);
	return 0;
}
