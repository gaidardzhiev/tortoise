/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <9p.h>

#define NFID9 128

static unsigned char inb[MAXMSG9P], outb[MAXMSG9P], datab[MAXMSG9P];

static int get_all(int fd, unsigned char *b, unsigned n) {
	unsigned done = 0;
	while (done < n) {
		int r = read(fd, b + done, n - done);
		if (r <= 0) return -1;
		done += (unsigned)r;
	}
	return 0;
}

static int put_all(int fd, unsigned char *b, unsigned n) {
	unsigned done = 0;
	while (done < n) {
		int r = write(fd, b + done, n - done);
		if (r <= 0) return -1;
		done += (unsigned)r;
	}
	return 0;
}

static Fid9 *lookup_fid(Srv9 *s, unsigned fid) {
	int i;
	for (i = 0; i < s->nfids; i++)
		if (s->fids[i].used && s->fids[i].fid == fid) return &s->fids[i];
	return NULL;
}

static Fid9 *new_fid(Srv9 *s, unsigned fid) {
	int i;
	if (lookup_fid(s, fid)) return NULL;
	for (i = 0; i < s->nfids; i++)
		if (!s->fids[i].used) {
			memset(&s->fids[i], 0, sizeof(Fid9));
			s->fids[i].used = 1;
			s->fids[i].fid = fid;
			s->fids[i].omode = -1;
			return &s->fids[i];
		}
	return NULL;
}

static void drop_fid(Srv9 *s, Fid9 *f) {
	if (s->clunk) s->clunk(s, f);
	f->used = 0;
}

const char *readbuf9(Fcall *in, Fcall *out, const char *src, unsigned len) {
	unsigned off = in->offset, n = in->count;
	if (in->offsethi || off >= len) n = 0;
	else if (n > len - off) n = len - off;
	out->count = n;
	out->data = (char *)src + (n ? off : 0);
	return NULL;
}

static const char *read_dir(Srv9 *s, Fid9 *f, Fcall *in, Fcall *out) {
	unsigned at = 0, max = in->count;
	const char *e;
	if (in->offset == 0) {
		f->diroff = 0;
		f->dirindex = 0;
	} else if (in->offset != f->diroff) return "bad offset in directory read";
	if (!s->dirent) return "cannot read directory";
	if (max > sizeof(datab)) max = sizeof(datab);
	for (;;) {
		Stat9 d;
		unsigned k;
		memset(&d, 0, sizeof(d));
		e = s->dirent(s, f, f->dirindex, &d);
		if (e) break;
		k = convD2M(&d, datab + at, max - at);
		if (k == 0) break;
		at += k;
		f->dirindex++;
	}
	f->diroff += at;
	out->count = at;
	out->data = (char *)datab;
	return NULL;
}

static const char *do_walk(Srv9 *s, Fcall *in, Fcall *out) {
	Fid9 *f = lookup_fid(s, in->fid), tmp, *nf;
	const char *e = NULL;
	int i;
	if (!f) return "unknown fid";
	if (f->omode != -1) return "cannot walk an open fid";
	if (in->newfid != in->fid && lookup_fid(s, in->newfid)) return "fid in use";
	tmp = *f;
	tmp.fid = in->newfid;
	tmp.omode = -1;
	if (s->clone && (e = s->clone(s, f, &tmp)) != NULL) return e;
	out->nwqid = 0;
	for (i = 0; i < in->nwname; i++) {
		Qid q;
		if (!(tmp.qid.type & QTDIR)) {
			e = "not a directory";
			break;
		}
		e = s->walk1(s, &tmp, in->wname[i], &q);
		if (e) break;
		tmp.qid = q;
		out->wqid[out->nwqid++] = q;
	}
	if (out->nwqid < in->nwname) {
		if (s->clunk) s->clunk(s, &tmp);
		return out->nwqid == 0 ? e : NULL;
	}
	if (in->newfid == in->fid) {
		if (s->clunk) s->clunk(s, f);
		*f = tmp;
		return NULL;
	}
	nf = new_fid(s, in->newfid);
	if (!nf) {
		if (s->clunk) s->clunk(s, &tmp);
		return "too many fids";
	}
	*nf = tmp;
	nf->used = 1;
	return NULL;
}

static const char *dispatch(Srv9 *s, Fcall *in, Fcall *out) {
	Fid9 *f = NULL;
	Stat9 d;
	const char *e;
	int i;
	out->type = in->type + 1;
	out->tag = in->tag;
	if (in->type != Tversion && in->type != Tauth && in->type != Tattach && in->type != Tflush && in->type != Twalk) {
		f = lookup_fid(s, in->fid);
		if (!f) return "unknown fid";
	}
	switch (in->type) {
	case Tversion:
		for (i = 0; i < s->nfids; i++)
			if (s->fids[i].used) drop_fid(s, &s->fids[i]);
		s->msize = in->msize < MAXMSG9P ? in->msize : MAXMSG9P;
		out->msize = s->msize;
		out->version = strncmp(in->version, VERSION9P, 6) ? "unknown" : VERSION9P;
		return NULL;
	case Tauth:
		return "authentication not required";
	case Tattach:
		if (in->afid != NOFID) return "authentication not required";
		f = new_fid(s, in->fid);
		if (!f) return "fid in use";
		e = s->attach(s, f, in, out);
		if (e) {
			f->used = 0;
			return e;
		}
		out->qid = f->qid;
		return NULL;
	case Tflush:
		return NULL;
	case Twalk:
		return do_walk(s, in, out);
	case Topen:
		if (f->omode != -1) return "fid already open";
		if ((f->qid.type & QTDIR) && (in->mode & 3) != OREAD) return "is a directory";
		out->qid = f->qid;
		out->iounit = 0;
		e = s->open ? s->open(s, f, in->mode, out) : NULL;
		if (!e) f->omode = in->mode;
		return e;
	case Tcreate:
		if (f->omode != -1) return "fid already open";
		if (!(f->qid.type & QTDIR)) return "not a directory";
		if (!s->create) return "permission denied";
		out->iounit = 0;
		e = s->create(s, f, in->name, in->perm, in->mode, out);
		if (!e) {
			f->omode = in->mode;
			out->qid = f->qid;
		}
		return e;
	case Tread:
		if (f->omode == -1 || (f->omode & 3) == OWRITE) return "fid not open for reading";
		if (in->count > s->msize - IOHDRSZ) in->count = s->msize - IOHDRSZ;
		if (f->qid.type & QTDIR) return read_dir(s, f, in, out);
		out->count = 0;
		out->data = (char *)datab;
		return s->read ? s->read(s, f, in, out) : "permission denied";
	case Twrite:
		if (f->omode == -1 || (f->omode & 3) == OREAD) return "fid not open for writing";
		if (f->qid.type & QTDIR) return "is a directory";
		return s->write ? s->write(s, f, in, out) : "permission denied";
	case Tclunk:
		if ((f->omode & ORCLOSE) && f->omode != -1 && s->remove) s->remove(s, f);
		drop_fid(s, f);
		return NULL;
	case Tremove:
		e = s->remove ? s->remove(s, f) : "permission denied";
		drop_fid(s, f);
		return e;
	case Tstat:
		memset(&d, 0, sizeof(d));
		e = s->stat(s, f, &d);
		if (e) return e;
		out->nstat = convD2M(&d, datab, sizeof(datab));
		out->stat = datab;
		return out->nstat ? NULL : "stat too long";
	case Twstat:
		if (convM2D(in->stat, in->nstat, &d) == 0) return "bad stat";
		return s->wstat ? s->wstat(s, f, &d) : "permission denied";
	}
	return "bad message";
}

int serve9(Srv9 *s) {
	if (!s->fids) {
		s->nfids = NFID9;
		s->fids = calloc(NFID9, sizeof(Fid9));
		if (!s->fids) return -1;
	}
	if (!s->msize) s->msize = MAXMSG9P;
	for (;;) {
		Fcall in, out;
		unsigned size, n;
		const char *e;
		if (get_all(s->infd, inb, 4)) return 0;
		size = (unsigned)inb[0] | ((unsigned)inb[1] << 8) | ((unsigned)inb[2] << 16) | ((unsigned)inb[3] << 24);
		if (size < 7 || size > sizeof(inb) || get_all(s->infd, inb + 4, size - 4)) return -1;
		memset(&in, 0, sizeof(in));
		memset(&out, 0, sizeof(out));
		if (convM2S(inb, size, &in) != size) return -1;
		e = dispatch(s, &in, &out);
		if (s->debug) fprintf(stderr, "9p: type %d tag %d%s%s\n", in.type, in.tag, e ? " error " : "", e ? e : "");
		if (e) {
			memset(&out, 0, sizeof(out));
			out.type = Rerror;
			out.tag = in.tag;
			out.ename = (char *)e;
		}
		n = convS2M(&out, outb, sizeof(outb));
		if (n == 0 || put_all(s->outfd, outb, n)) return -1;
	}
}
