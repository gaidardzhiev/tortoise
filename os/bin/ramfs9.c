/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <signal.h>
#include <sys/socket.h>
#include <plan9.h>
#include <9p.h>

#define NNODE 512
#define MAXFILE (1024 * 1024)

typedef struct Node {
	int used, parent, refs, removed;
	char name[28];
	unsigned path, vers, perm;
	char uid[12], gid[12], muid[12];
	unsigned atime, mtime;
	char *data;
	unsigned len, cap;
} Node;

static Node nodes[NNODE];
static unsigned nextpath = 1;
static char user[12] = "0";

static int isdir(Node *n) {
	return (n->perm & DMDIR) != 0;
}

static Qid qid_of(int i) {
	Qid q;
	q.type = (nodes[i].perm & DMDIR) ? QTDIR : (nodes[i].perm & DMSYMLINK) ? QTSYMLINK : QTFILE;
	q.vers = nodes[i].vers;
	q.path = nodes[i].path;
	q.pathhi = 0;
	return q;
}

static int idx(Fid9 *f) {
	return (int)(long)f->aux;
}

static void hold(Fid9 *f, int i) {
	f->aux = (void *)(long)i;
	f->qid = qid_of(i);
	nodes[i].refs++;
}

static void release(int i) {
	if (--nodes[i].refs <= 0 && nodes[i].removed) {
		free(nodes[i].data);
		nodes[i].used = 0;
	}
}

static int new_node(int parent, const char *name, unsigned perm) {
	int i;
	for (i = 1; i < NNODE; i++)
		if (!nodes[i].used) {
			memset(&nodes[i], 0, sizeof(Node));
			nodes[i].used = 1;
			nodes[i].parent = parent;
			strncpy(nodes[i].name, name, 27);
			nodes[i].path = nextpath++;
			nodes[i].perm = perm;
			strcpy(nodes[i].uid, user);
			strcpy(nodes[i].gid, user);
			strcpy(nodes[i].muid, user);
			nodes[i].atime = nodes[i].mtime = (unsigned)time();
			return i;
		}
	return -1;
}

static int child(int dir, const char *name) {
	int i;
	for (i = 1; i < NNODE; i++)
		if (nodes[i].used && !nodes[i].removed && nodes[i].parent == dir && !strcmp(nodes[i].name, name)) return i;
	return -1;
}

static const char *fs_attach(Srv9 *s, Fid9 *f, Fcall *in, Fcall *out) {
	(void)s;
	(void)out;
	strncpy(user, in->uname, sizeof(user) - 1);
	hold(f, 0);
	return NULL;
}

static const char *fs_clone(Srv9 *s, Fid9 *old, Fid9 *f) {
	(void)s;
	hold(f, idx(old));
	return NULL;
}

static const char *fs_walk1(Srv9 *s, Fid9 *f, const char *name, Qid *q) {
	int i = idx(f), c;
	(void)s;
	if (!strcmp(name, "..")) c = nodes[i].parent;
	else if (!strcmp(name, ".")) c = i;
	else c = child(i, name);
	if (c < 0) return "file does not exist";
	release(i);
	hold(f, c);
	*q = f->qid;
	return NULL;
}

static void fs_clunk(Srv9 *s, Fid9 *f) {
	(void)s;
	release(idx(f));
}

static const char *fs_open(Srv9 *s, Fid9 *f, int mode, Fcall *out) {
	Node *n = &nodes[idx(f)];
	(void)s;
	(void)out;
	if ((mode & OTRUNC) && !isdir(n)) {
		n->len = 0;
		n->vers++;
		n->mtime = (unsigned)time();
	}
	return NULL;
}

static const char *fs_create(Srv9 *s, Fid9 *f, const char *name, unsigned perm, int mode, Fcall *out) {
	int d = idx(f), n;
	(void)s;
	(void)mode;
	(void)out;
	if (!strcmp(name, ".") || !strcmp(name, "..")) return "invalid argument";
	if (strlen(name) > 27) return "name too long";
	if (child(d, name) >= 0) return "file already exists";
	n = new_node(d, name, perm & (DMDIR | DMSYMLINK | 0777));
	if (n < 0) return "no space left";
	nodes[d].mtime = (unsigned)time();
	release(d);
	hold(f, n);
	return NULL;
}

static const char *fs_read(Srv9 *s, Fid9 *f, Fcall *in, Fcall *out) {
	Node *n = &nodes[idx(f)];
	(void)s;
	n->atime = (unsigned)time();
	return readbuf9(in, out, n->data ? n->data : "", n->len);
}

static int grow(Node *n, unsigned len) {
	if (len > MAXFILE) return -1;
	if (len > n->cap) {
		unsigned cap = n->cap ? n->cap : 256;
		char *d;
		while (cap < len) cap *= 2;
		d = realloc(n->data, cap);
		if (!d) return -1;
		n->data = d;
		n->cap = cap;
	}
	if (len > n->len) memset(n->data + n->len, 0, len - n->len);
	n->len = len;
	return 0;
}

static const char *fs_write(Srv9 *s, Fid9 *f, Fcall *in, Fcall *out) {
	Node *n = &nodes[idx(f)];
	unsigned end = in->offset + in->count;
	(void)s;
	if (in->offsethi || end < in->offset) return "bad offset";
	if (end > n->len && grow(n, end)) return "no space left";
	memcpy(n->data + in->offset, in->data, in->count);
	n->vers++;
	n->mtime = (unsigned)time();
	strcpy(n->muid, user);
	out->count = in->count;
	return NULL;
}

static const char *fs_remove(Srv9 *s, Fid9 *f) {
	int i = idx(f), k;
	(void)s;
	if (i == 0) return "permission denied";
	if (isdir(&nodes[i]))
		for (k = 1; k < NNODE; k++)
			if (nodes[k].used && !nodes[k].removed && nodes[k].parent == i) return "directory not empty";
	nodes[i].removed = 1;
	nodes[nodes[i].parent].mtime = (unsigned)time();
	return NULL;
}

static void fill(int i, Stat9 *d) {
	Node *n = &nodes[i];
	memset(d, 0, sizeof(*d));
	d->qid = qid_of(i);
	d->mode = n->perm;
	d->atime = n->atime;
	d->mtime = n->mtime;
	d->length = isdir(n) ? 0 : n->len;
	d->name = i == 0 ? "/" : n->name;
	d->uid = n->uid;
	d->gid = n->gid;
	d->muid = n->muid;
}

static const char *fs_stat(Srv9 *s, Fid9 *f, Stat9 *d) {
	(void)s;
	fill(idx(f), d);
	return NULL;
}

static const char *fs_wstat(Srv9 *s, Fid9 *f, Stat9 *d) {
	int i = idx(f);
	Node *n = &nodes[i];
	(void)s;
	if (d->name[0] && strcmp(d->name, n->name)) {
		if (i == 0) return "permission denied";
		if (strlen(d->name) > 27) return "name too long";
		if (child(n->parent, d->name) >= 0) return "file already exists";
	}
	if (d->length != 0xFFFFFFFFu) {
		if (isdir(n)) return "is a directory";
		if (d->length < n->len) n->len = d->length;
		else if (d->length > n->len && grow(n, d->length)) return "no space left";
		n->vers++;
		n->mtime = (unsigned)time();
	}
	if (d->name[0]) strncpy(n->name, d->name, 27);
	if (d->mode != 0xFFFFFFFFu) n->perm = (n->perm & (DMDIR | DMSYMLINK)) | (d->mode & 0777);
	if (d->atime != 0xFFFFFFFFu) n->atime = d->atime;
	if (d->mtime != 0xFFFFFFFFu) n->mtime = d->mtime;
	if (d->uid[0]) strncpy(n->uid, d->uid, 11);
	if (d->gid[0]) strncpy(n->gid, d->gid, 11);
	return NULL;
}

static const char *fs_dirent(Srv9 *s, Fid9 *f, unsigned index, Stat9 *d) {
	int dir = idx(f), k;
	unsigned seen = 0;
	(void)s;
	for (k = 1; k < NNODE; k++)
		if (nodes[k].used && !nodes[k].removed && nodes[k].parent == dir && k != dir) {
			if (seen++ == index) {
				fill(k, d);
				return NULL;
			}
		}
	return "end";
}

static Srv9 fs = {
	fs_attach, fs_walk1, fs_clone, fs_open, fs_create, fs_read, fs_write, fs_remove,
	fs_stat, fs_wstat, fs_clunk, fs_dirent, 0, 1, 0, NULL, NULL, 0, 0
};

static int post(const char *name, int fd) {
	char path[64], num[12];
	int s;
	snprintf(path, sizeof(path), "/srv/%s", name);
	s = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
	if (s < 0) return -1;
	snprintf(num, sizeof(num), "%d", fd);
	if (write(s, num, strlen(num)) < 0) {
		close(s);
		return -1;
	}
	close(s);
	return 0;
}

int main(int argc, char **argv) {
	const char *srvname = NULL, *mtpt = NULL;
	int i, p[2], pid;
	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-s") && i + 1 < argc) srvname = argv[++i];
		else if (!strcmp(argv[i], "-m") && i + 1 < argc) mtpt = argv[++i];
		else if (!strcmp(argv[i], "-D")) fs.debug = 1;
		else {
			fprintf(stderr, "usage: ramfs9 [-D] [-s srvname] [-m mtpt]\n");
			return 1;
		}
	}
	memset(nodes, 0, sizeof(nodes));
	nodes[0].used = 1;
	nodes[0].path = 0;
	nodes[0].perm = DMDIR | 0777;
	nodes[0].atime = nodes[0].mtime = (unsigned)time();
	nodes[0].refs = 1;
	strcpy(nodes[0].uid, "0");
	strcpy(nodes[0].gid, "0");
	strcpy(nodes[0].muid, "0");
	if (!srvname && !mtpt) return serve9(&fs) < 0;
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, p) < 0) {
		perror("ramfs9: socketpair");
		return 1;
	}
	pid = fork();
	if (pid < 0) {
		perror("ramfs9: fork");
		return 1;
	}
	if (pid == 0) {
		close(p[1]);
		fs.infd = fs.outfd = p[0];
		signal(SIGINT, SIG_IGN);
		_exit(serve9(&fs) < 0);
	}
	close(p[0]);
	if (srvname && post(srvname, p[1]) < 0) {
		fprintf(stderr, "ramfs9: cannot post /srv/%s\n", srvname);
		return 1;
	}
	if (mtpt && mount9(p[1], -1, mtpt, MREPLACE | MCREATE, "") < 0) {
		char e[ERRMAX];
		e[0] = 0;
		errstr(e, sizeof(e));
		fprintf(stderr, "ramfs9: mount %s: %s\n", mtpt, e);
		return 1;
	}
	close(p[1]);
	return 0;
}
