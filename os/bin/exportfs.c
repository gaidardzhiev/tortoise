/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <signal.h>
#include <utime.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <9p.h>

#define PORT9 17007
#define MAXP 512

typedef struct Aux {
	char path[MAXP];
	int fd;
	DIR *dir;
	int symlink;
} Aux;

static char root[MAXP] = "/";

static Aux *aux(Fid9 *f) {
	return f->aux;
}

static const char *err(void) {
	switch (errno) {
	case ENOENT:
		return "file does not exist";
	case EACCES:
		return "permission denied";
	case EPERM:
		return "operation not permitted";
	case EEXIST:
		return "file already exists";
	case ENOTDIR:
		return "not a directory";
	case EISDIR:
		return "is a directory";
	case ENOTEMPTY:
		return "directory not empty";
	case EBUSY:
		return "file in use";
	case EROFS:
		return "read only file system";
	case ENAMETOOLONG:
		return "name too long";
	case ENOSPC:
		return "no space left";
	case EINVAL:
		return "invalid argument";
	}
	return strerror(errno);
}

static Qid qid_of(struct stat *st) {
	Qid q;
	q.type = S_ISDIR(st->st_mode) ? QTDIR : S_ISLNK(st->st_mode) ? QTSYMLINK : QTFILE;
	q.vers = (unsigned)st->st_mtime;
	q.path = (unsigned)st->st_ino ^ ((unsigned)st->st_dev << 20);
	q.pathhi = 0;
	return q;
}

static char *full(const char *p, const char *name, char *out) {
	if (!strcmp(p, "/")) snprintf(out, MAXP, "/%s", name);
	else snprintf(out, MAXP, "%s/%s", p, name);
	return out;
}

static Aux *new_aux(const char *path) {
	Aux *a = calloc(1, sizeof(Aux));
	if (!a) return NULL;
	strncpy(a->path, path, MAXP - 1);
	a->fd = -1;
	return a;
}

static void close_aux(Aux *a) {
	if (a->fd >= 0) close(a->fd);
	if (a->dir) closedir(a->dir);
	a->fd = -1;
	a->dir = NULL;
}

static const char *x_attach(Srv9 *s, Fid9 *f, Fcall *in, Fcall *out) {
	struct stat st;
	(void)s;
	(void)in;
	(void)out;
	if (lstat(root, &st) < 0) return err();
	f->aux = new_aux(root);
	if (!f->aux) return "out of memory";
	f->qid = qid_of(&st);
	return NULL;
}

static const char *x_clone(Srv9 *s, Fid9 *old, Fid9 *f) {
	(void)s;
	f->aux = new_aux(aux(old)->path);
	return f->aux ? NULL : "out of memory";
}

static const char *x_walk1(Srv9 *s, Fid9 *f, const char *name, Qid *q) {
	Aux *a = aux(f);
	char p[MAXP];
	struct stat st;
	(void)s;
	if (!strcmp(name, "..")) {
		char *slash;
		strcpy(p, a->path);
		if (strcmp(p, root)) {
			slash = strrchr(p, '/');
			if (slash == p) p[1] = 0;
			else if (slash) *slash = 0;
		}
	} else if (!strcmp(name, ".")) strcpy(p, a->path);
	else full(a->path, name, p);
	if (lstat(p, &st) < 0) return err();
	strcpy(a->path, p);
	*q = qid_of(&st);
	return NULL;
}

static void x_clunk(Srv9 *s, Fid9 *f) {
	(void)s;
	if (!f->aux) return;
	close_aux(aux(f));
	free(f->aux);
	f->aux = NULL;
}

static int oflags(int mode) {
	int m = mode & 3, fl = m == OWRITE ? O_WRONLY : m == ORDWR ? O_RDWR : O_RDONLY;
	if (mode & OTRUNC) fl |= O_TRUNC;
	return fl;
}

static const char *x_open(Srv9 *s, Fid9 *f, int mode, Fcall *out) {
	Aux *a = aux(f);
	(void)s;
	(void)out;
	if (f->qid.type & QTDIR) {
		a->dir = opendir(a->path);
		return a->dir ? NULL : err();
	}
	if (f->qid.type & QTSYMLINK) return NULL;
	a->fd = open(a->path, oflags(mode));
	return a->fd >= 0 ? NULL : err();
}

static const char *x_create(Srv9 *s, Fid9 *f, const char *name, unsigned perm, int mode, Fcall *out) {
	Aux *a = aux(f);
	char p[MAXP];
	struct stat st;
	(void)s;
	(void)out;
	full(a->path, name, p);
	if (perm & DMDIR) {
		if (mkdir(p, perm & 0777) < 0) return err();
		a->dir = opendir(p);
	} else if (perm & DMSYMLINK) {
		if (lstat(p, &st) == 0) return "file already exists";
		a->symlink = 1;
		strcpy(a->path, p);
		f->qid.type = QTSYMLINK;
		f->qid.vers = 0;
		f->qid.path = 0;
		f->qid.pathhi = 0;
		return NULL;
	} else {
		a->fd = open(p, oflags(mode) | O_CREAT | O_EXCL, 0x80000000u | (perm & 0777));
		if (a->fd < 0) return err();
	}
	strcpy(a->path, p);
	if (lstat(p, &st) < 0) return err();
	f->qid = qid_of(&st);
	return NULL;
}

static char rbuf[MAXMSG9P];

static const char *x_read(Srv9 *s, Fid9 *f, Fcall *in, Fcall *out) {
	Aux *a = aux(f);
	int n;
	(void)s;
	if (f->qid.type & QTSYMLINK) {
		n = readlink(a->path, rbuf, sizeof(rbuf));
		if (n < 0) return err();
		return readbuf9(in, out, rbuf, (unsigned)n);
	}
	if (lseek(a->fd, (int)in->offset, SEEK_SET) < 0) return err();
	n = read(a->fd, rbuf, in->count > sizeof(rbuf) ? sizeof(rbuf) : in->count);
	if (n < 0) return err();
	out->count = (unsigned)n;
	out->data = rbuf;
	return NULL;
}

static const char *x_write(Srv9 *s, Fid9 *f, Fcall *in, Fcall *out) {
	Aux *a = aux(f);
	int n;
	(void)s;
	if (a->symlink) {
		char t[MAXP];
		struct stat st;
		unsigned k = in->count < MAXP - 1 ? in->count : MAXP - 1;
		memcpy(t, in->data, k);
		t[k] = 0;
		if (symlink(t, a->path) < 0) return err();
		a->symlink = 0;
		if (lstat(a->path, &st) == 0) f->qid = qid_of(&st);
		out->count = in->count;
		return NULL;
	}
	if (lseek(a->fd, (int)in->offset, SEEK_SET) < 0) return err();
	n = write(a->fd, in->data, in->count);
	if (n < 0) return err();
	out->count = (unsigned)n;
	return NULL;
}

static const char *x_remove(Srv9 *s, Fid9 *f) {
	Aux *a = aux(f);
	(void)s;
	close_aux(a);
	if ((f->qid.type & QTDIR) ? rmdir(a->path) < 0 : unlink(a->path) < 0) return err();
	return NULL;
}

static char sname[256], suid[12], sgid[12];

static void fill(const char *path, struct stat *st, const char *name, Stat9 *d) {
	(void)path;
	memset(d, 0, sizeof(*d));
	d->qid = qid_of(st);
	d->mode = (st->st_mode & 0777) | (S_ISDIR(st->st_mode) ? DMDIR : 0) | (S_ISLNK(st->st_mode) ? DMSYMLINK : 0);
	d->atime = (unsigned)st->st_atime;
	d->mtime = (unsigned)st->st_mtime;
	d->length = S_ISDIR(st->st_mode) ? 0 : (unsigned)st->st_size;
	strncpy(sname, name, sizeof(sname) - 1);
	snprintf(suid, sizeof(suid), "%d", (int)st->st_uid);
	snprintf(sgid, sizeof(sgid), "%d", (int)st->st_gid);
	d->name = sname;
	d->uid = suid;
	d->gid = sgid;
	d->muid = suid;
}

static const char *base(const char *p) {
	const char *s = strrchr(p, '/');
	return s && s[1] ? s + 1 : "/";
}

static const char *x_stat(Srv9 *s, Fid9 *f, Stat9 *d) {
	Aux *a = aux(f);
	struct stat st;
	(void)s;
	if (lstat(a->path, &st) < 0) return err();
	fill(a->path, &st, base(a->path), d);
	return NULL;
}

static const char *x_wstat(Srv9 *s, Fid9 *f, Stat9 *d) {
	Aux *a = aux(f);
	struct stat st;
	(void)s;
	if (lstat(a->path, &st) < 0) return err();
	if (d->length != 0xFFFFFFFFu && truncate(a->path, (int)d->length) < 0) return err();
	if (d->mode != 0xFFFFFFFFu && chmod(a->path, d->mode & 0777) < 0) return err();
	if (d->mtime != 0xFFFFFFFFu || d->atime != 0xFFFFFFFFu) {
		struct utimbuf ut;
		ut.actime = d->atime != 0xFFFFFFFFu ? (int)d->atime : st.st_atime;
		ut.modtime = d->mtime != 0xFFFFFFFFu ? (int)d->mtime : st.st_mtime;
		if (utime(a->path, &ut) < 0) return err();
	}
	if (d->uid[0] || d->gid[0]) {
		int u = d->uid[0] ? atoi(d->uid) : -1, g = d->gid[0] ? atoi(d->gid) : -1;
		if (chown(a->path, u, g) < 0) return err();
	}
	if (d->name[0] && strcmp(d->name, base(a->path))) {
		char p[MAXP], *slash;
		strcpy(p, a->path);
		slash = strrchr(p, '/');
		if (!slash || strchr(d->name, '/')) return "invalid argument";
		strcpy(slash + 1, d->name);
		if (rename(a->path, p) < 0) return err();
		strcpy(a->path, p);
	}
	return NULL;
}

static const char *x_dirent(Srv9 *s, Fid9 *f, unsigned index, Stat9 *d) {
	Aux *a = aux(f);
	struct dirent *e;
	struct stat st;
	char p[MAXP];
	(void)s;
	if (!a->dir) return "end";
	if (index == 0) rewinddir(a->dir);
	for (;;) {
		e = readdir(a->dir);
		if (!e) return "end";
		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
		if (lstat(full(a->path, e->d_name, p), &st) < 0) continue;
		fill(p, &st, e->d_name, d);
		return NULL;
	}
}

static Srv9 fs = {
	x_attach, x_walk1, x_clone, x_open, x_create, x_read, x_write, x_remove,
	x_stat, x_wstat, x_clunk, x_dirent, 0, 1, 0, NULL, NULL, 0, 0
};

static int getline_fd(int fd, char *b, int n) {
	int i = 0;
	while (i < n - 1) {
		char c;
		if (read(fd, &c, 1) != 1) return -1;
		if (c == '\n') break;
		b[i++] = c;
	}
	b[i] = 0;
	return i;
}

static int negotiate(int fd) {
	char line[MAXP];
	struct stat st;
	if (getline_fd(fd, line, sizeof(line)) < 0) return -1;
	if (line[0] != '/' || stat(line, &st) < 0 || !S_ISDIR(st.st_mode)) {
		write(fd, "no such directory\n", 18);
		return -1;
	}
	strcpy(root, line);
	write(fd, "OK\n", 3);
	return 0;
}

static int announce_port(int port) {
	struct sockaddr_in a;
	int s = socket(AF_INET, SOCK_STREAM, 0), one = 1;
	if (s < 0) return -1;
	setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	memset(&a, 0, sizeof(a));
	a.sin_family = AF_INET;
	a.sin_port = htons((unsigned short)port);
	a.sin_addr.s_addr = 0;
	if (bind(s, (struct sockaddr *)&a, sizeof(a)) < 0 || listen(s, 4) < 0) {
		close(s);
		return -1;
	}
	return s;
}

int main(int argc, char **argv) {
	int i, sflag = 0, lflag = 0, fd = -1;
	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-r") && i + 1 < argc) strncpy(root, argv[++i], MAXP - 1);
		else if (!strcmp(argv[i], "-d") && i + 1 < argc) fd = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-s")) sflag = 1;
		else if (!strcmp(argv[i], "-l")) lflag = 1;
		else if (!strcmp(argv[i], "-D")) fs.debug = 1;
		else {
			fprintf(stderr, "usage: exportfs [-D] [-r root] [-d fd] [-s] [-l]\n");
			return 1;
		}
	}
	if (lflag) {
		int ls = announce_port(PORT9);
		if (ls < 0) {
			perror("exportfs: listen");
			return 1;
		}
		signal(SIGCHLD, SIG_IGN);
		for (;;) {
			int c = accept(ls, NULL, NULL), pid;
			if (c < 0) continue;
			pid = fork();
			if (pid == 0) {
				close(ls);
				if (negotiate(c) < 0) _exit(1);
				fs.infd = fs.outfd = c;
				_exit(serve9(&fs) < 0);
			}
			close(c);
		}
	}
	if (fd >= 0) fs.infd = fs.outfd = fd;
	if (sflag && negotiate(fs.infd) < 0) return 1;
	return serve9(&fs) < 0;
}
