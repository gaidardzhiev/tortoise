/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include "include/9p.h"

static unsigned slen(const char *s) {
	unsigned n = 0;
	if (s)
		while (s[n]) n++;
	return n;
}

static unsigned char *p8(unsigned char *p, unsigned v) {
	*p++ = (unsigned char)v;
	return p;
}

static unsigned char *p16(unsigned char *p, unsigned v) {
	p[0] = (unsigned char)v;
	p[1] = (unsigned char)(v >> 8);
	return p + 2;
}

static unsigned char *p32(unsigned char *p, unsigned v) {
	p[0] = (unsigned char)v;
	p[1] = (unsigned char)(v >> 8);
	p[2] = (unsigned char)(v >> 16);
	p[3] = (unsigned char)(v >> 24);
	return p + 4;
}

static unsigned char *pstr(unsigned char *p, const char *s) {
	unsigned n = slen(s), i;
	p = p16(p, n);
	for (i = 0; i < n; i++) *p++ = (unsigned char)s[i];
	return p;
}

static unsigned char *pqid(unsigned char *p, Qid *q) {
	p = p8(p, q->type);
	p = p32(p, q->vers);
	p = p32(p, q->path);
	return p32(p, q->pathhi);
}

static unsigned g16(unsigned char *p) {
	return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

static unsigned g32(unsigned char *p) {
	return (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}

static unsigned char *gstr(unsigned char *p, unsigned char *end, char **s) {
	unsigned n, i;
	if (!p || p + 2 > end) return 0;
	n = g16(p);
	if (p + 2 + n > end) return 0;
	for (i = 0; i < n; i++) p[i] = p[i + 2];
	p[n] = 0;
	*s = (char *)p;
	return p + 2 + n;
}

static unsigned char *gqid(unsigned char *p, unsigned char *end, Qid *q) {
	if (!p || p + 13 > end) return 0;
	q->type = p[0];
	q->vers = g32(p + 1);
	q->path = g32(p + 5);
	q->pathhi = g32(p + 9);
	return p + 13;
}

static unsigned char *gn(unsigned char *p, unsigned char *end, unsigned k) {
	if (!p || p + k > end) return 0;
	return p;
}

unsigned sizeD2M(Stat9 *d) {
	return 2 + 2 + 4 + 13 + 4 + 4 + 4 + 8 + 8 + slen(d->name) + slen(d->uid) + slen(d->gid) + slen(d->muid);
}

unsigned convD2M(Stat9 *d, unsigned char *buf, unsigned n) {
	unsigned size = sizeD2M(d);
	unsigned char *p = buf;
	if (size > n) return 0;
	p = p16(p, size - 2);
	p = p16(p, d->type);
	p = p32(p, d->dev);
	p = pqid(p, &d->qid);
	p = p32(p, d->mode);
	p = p32(p, d->atime);
	p = p32(p, d->mtime);
	p = p32(p, d->length);
	p = p32(p, d->lengthhi);
	p = pstr(p, d->name);
	p = pstr(p, d->uid);
	p = pstr(p, d->gid);
	pstr(p, d->muid);
	return size;
}

unsigned convM2D(unsigned char *buf, unsigned n, Stat9 *d) {
	unsigned char *p = buf, *end = buf + n;
	unsigned size;
	if (n < 2) return 0;
	size = g16(p) + 2;
	if (size > n) return 0;
	end = buf + size;
	p += 2;
	if (!gn(p, end, 39)) return 0;
	d->type = (unsigned short)g16(p);
	d->dev = g32(p + 2);
	gqid(p + 6, end, &d->qid);
	d->mode = g32(p + 19);
	d->atime = g32(p + 23);
	d->mtime = g32(p + 27);
	d->length = g32(p + 31);
	d->lengthhi = g32(p + 35);
	p += 39;
	p = gstr(p, end, &d->name);
	p = gstr(p, end, &d->uid);
	p = gstr(p, end, &d->gid);
	p = gstr(p, end, &d->muid);
	return p ? size : 0;
}

void nulldir9(Stat9 *d) {
	d->type = 0xFFFF;
	d->dev = 0xFFFFFFFFu;
	d->qid.type = 0xFF;
	d->qid.vers = 0xFFFFFFFFu;
	d->qid.path = d->qid.pathhi = 0xFFFFFFFFu;
	d->mode = d->atime = d->mtime = 0xFFFFFFFFu;
	d->length = d->lengthhi = 0xFFFFFFFFu;
	d->name = d->uid = d->gid = d->muid = "";
}

unsigned sizeS2M(Fcall *f) {
	unsigned n = 4 + 1 + 2;
	int i;
	switch (f->type) {
	case Tversion:
	case Rversion:
		return n + 4 + 2 + slen(f->version);
	case Tauth:
		return n + 4 + 2 + slen(f->uname) + 2 + slen(f->aname);
	case Rauth:
	case Rattach:
		return n + 13;
	case Tattach:
		return n + 4 + 4 + 2 + slen(f->uname) + 2 + slen(f->aname);
	case Rerror:
		return n + 2 + slen(f->ename);
	case Tflush:
		return n + 2;
	case Twalk:
		n += 4 + 4 + 2;
		for (i = 0; i < f->nwname; i++) n += 2 + slen(f->wname[i]);
		return n;
	case Rwalk:
		return n + 2 + 13 * (unsigned)f->nwqid;
	case Topen:
		return n + 4 + 1;
	case Ropen:
	case Rcreate:
		return n + 13 + 4;
	case Tcreate:
		return n + 4 + 2 + slen(f->name) + 4 + 1;
	case Tread:
		return n + 4 + 8 + 4;
	case Rread:
		return n + 4 + f->count;
	case Twrite:
		return n + 4 + 8 + 4 + f->count;
	case Rwrite:
		return n + 4;
	case Tclunk:
	case Tremove:
	case Tstat:
		return n + 4;
	case Rstat:
		return n + 2 + f->nstat;
	case Twstat:
		return n + 4 + 2 + f->nstat;
	case Rflush:
	case Rclunk:
	case Rremove:
	case Rwstat:
		return n;
	}
	return 0;
}

unsigned convS2M(Fcall *f, unsigned char *buf, unsigned n) {
	unsigned size = sizeS2M(f), i;
	unsigned char *p = buf;
	int k;
	if (size == 0 || size > n) return 0;
	p = p32(p, size);
	p = p8(p, f->type);
	p = p16(p, f->tag);
	switch (f->type) {
	case Tversion:
	case Rversion:
		p = p32(p, f->msize);
		pstr(p, f->version);
		break;
	case Tauth:
		p = p32(p, f->afid);
		p = pstr(p, f->uname);
		pstr(p, f->aname);
		break;
	case Rauth:
	case Rattach:
		pqid(p, &f->qid);
		break;
	case Tattach:
		p = p32(p, f->fid);
		p = p32(p, f->afid);
		p = pstr(p, f->uname);
		pstr(p, f->aname);
		break;
	case Rerror:
		pstr(p, f->ename);
		break;
	case Tflush:
		p16(p, f->oldtag);
		break;
	case Twalk:
		p = p32(p, f->fid);
		p = p32(p, f->newfid);
		p = p16(p, (unsigned)f->nwname);
		for (k = 0; k < f->nwname; k++) p = pstr(p, f->wname[k]);
		break;
	case Rwalk:
		p = p16(p, (unsigned)f->nwqid);
		for (k = 0; k < f->nwqid; k++) p = pqid(p, &f->wqid[k]);
		break;
	case Topen:
		p = p32(p, f->fid);
		p8(p, f->mode);
		break;
	case Ropen:
	case Rcreate:
		p = pqid(p, &f->qid);
		p32(p, f->iounit);
		break;
	case Tcreate:
		p = p32(p, f->fid);
		p = pstr(p, f->name);
		p = p32(p, f->perm);
		p8(p, f->mode);
		break;
	case Tread:
		p = p32(p, f->fid);
		p = p32(p, f->offset);
		p = p32(p, f->offsethi);
		p32(p, f->count);
		break;
	case Rread:
		p = p32(p, f->count);
		for (i = 0; i < f->count; i++) *p++ = (unsigned char)f->data[i];
		break;
	case Twrite:
		p = p32(p, f->fid);
		p = p32(p, f->offset);
		p = p32(p, f->offsethi);
		p = p32(p, f->count);
		for (i = 0; i < f->count; i++) *p++ = (unsigned char)f->data[i];
		break;
	case Rwrite:
		p32(p, f->count);
		break;
	case Tclunk:
	case Tremove:
	case Tstat:
		p32(p, f->fid);
		break;
	case Rstat:
		p = p16(p, f->nstat);
		for (i = 0; i < f->nstat; i++) *p++ = f->stat[i];
		break;
	case Twstat:
		p = p32(p, f->fid);
		p = p16(p, f->nstat);
		for (i = 0; i < f->nstat; i++) *p++ = f->stat[i];
		break;
	}
	return size;
}

unsigned convM2S(unsigned char *buf, unsigned n, Fcall *f) {
	unsigned char *p = buf + 7, *end;
	unsigned size;
	int k;
	if (n < 7) return 0;
	size = g32(buf);
	if (size < 7 || size > n) return 0;
	end = buf + size;
	f->type = buf[4];
	f->tag = (unsigned short)g16(buf + 5);
	switch (f->type) {
	case Tversion:
	case Rversion:
		if (!gn(p, end, 4)) return 0;
		f->msize = g32(p);
		p = gstr(p + 4, end, &f->version);
		break;
	case Tauth:
		if (!gn(p, end, 4)) return 0;
		f->afid = g32(p);
		p = gstr(p + 4, end, &f->uname);
		p = gstr(p, end, &f->aname);
		break;
	case Rauth:
	case Rattach:
		p = gqid(p, end, &f->qid);
		break;
	case Tattach:
		if (!gn(p, end, 8)) return 0;
		f->fid = g32(p);
		f->afid = g32(p + 4);
		p = gstr(p + 8, end, &f->uname);
		p = gstr(p, end, &f->aname);
		break;
	case Rerror:
		p = gstr(p, end, &f->ename);
		break;
	case Tflush:
		if (!gn(p, end, 2)) return 0;
		f->oldtag = (unsigned short)g16(p);
		p += 2;
		break;
	case Twalk:
		if (!gn(p, end, 10)) return 0;
		f->fid = g32(p);
		f->newfid = g32(p + 4);
		f->nwname = (int)g16(p + 8);
		if (f->nwname > MAXWELEM) return 0;
		p += 10;
		for (k = 0; k < f->nwname; k++) p = gstr(p, end, &f->wname[k]);
		break;
	case Rwalk:
		if (!gn(p, end, 2)) return 0;
		f->nwqid = (int)g16(p);
		if (f->nwqid > MAXWELEM) return 0;
		p += 2;
		for (k = 0; k < f->nwqid; k++) p = gqid(p, end, &f->wqid[k]);
		break;
	case Topen:
		if (!gn(p, end, 5)) return 0;
		f->fid = g32(p);
		f->mode = p[4];
		p += 5;
		break;
	case Ropen:
	case Rcreate:
		p = gqid(p, end, &f->qid);
		if (!gn(p, end, 4)) return 0;
		f->iounit = g32(p);
		p += 4;
		break;
	case Tcreate:
		if (!gn(p, end, 4)) return 0;
		f->fid = g32(p);
		p = gstr(p + 4, end, &f->name);
		if (!gn(p, end, 5)) return 0;
		f->perm = g32(p);
		f->mode = p[4];
		p += 5;
		break;
	case Tread:
		if (!gn(p, end, 16)) return 0;
		f->fid = g32(p);
		f->offset = g32(p + 4);
		f->offsethi = g32(p + 8);
		f->count = g32(p + 12);
		p += 16;
		break;
	case Rread:
		if (!gn(p, end, 4)) return 0;
		f->count = g32(p);
		if (!gn(p + 4, end, f->count)) return 0;
		f->data = (char *)p + 4;
		p += 4 + f->count;
		break;
	case Twrite:
		if (!gn(p, end, 16)) return 0;
		f->fid = g32(p);
		f->offset = g32(p + 4);
		f->offsethi = g32(p + 8);
		f->count = g32(p + 12);
		if (!gn(p + 16, end, f->count)) return 0;
		f->data = (char *)p + 16;
		p += 16 + f->count;
		break;
	case Rwrite:
		if (!gn(p, end, 4)) return 0;
		f->count = g32(p);
		p += 4;
		break;
	case Tclunk:
	case Tremove:
	case Tstat:
		if (!gn(p, end, 4)) return 0;
		f->fid = g32(p);
		p += 4;
		break;
	case Rstat:
		if (!gn(p, end, 2)) return 0;
		f->nstat = g16(p);
		if (!gn(p + 2, end, f->nstat)) return 0;
		f->stat = p + 2;
		p += 2 + f->nstat;
		break;
	case Twstat:
		if (!gn(p, end, 6)) return 0;
		f->fid = g32(p);
		f->nstat = g16(p + 4);
		if (!gn(p + 6, end, f->nstat)) return 0;
		f->stat = p + 6;
		p += 6 + f->nstat;
		break;
	case Rflush:
	case Rclunk:
	case Rremove:
	case Rwstat:
		break;
	default:
		return 0;
	}
	if (!p || p != end) return 0;
	return size;
}
