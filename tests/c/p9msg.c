/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include "check.h"
#include <string.h>
#include "../../lib/9pconv.c"

static unsigned char out[256], copy[256];

static const unsigned char statbytes[] = {
	0x32, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x80, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0xED, 0x01, 0x00, 0x80, 0x0A, 0x00, 0x00, 0x00, 0x14, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x01, 0x00, '/', 0x01, 0x00, 'u', 0x01, 0x00, 'g', 0x00, 0x00
};

static int both_ways(Fcall *f, const unsigned char *want, unsigned n) {
	Fcall g;
	unsigned k = convS2M(f, out, sizeof(out));
	if (k != n || memcmp(out, want, n) != 0) return 0;
	memcpy(copy, want, n);
	memset(&g, 0, sizeof(g));
	if (convM2S(copy, n, &g) != n || g.type != f->type || g.tag != f->tag) return 0;
	k = convS2M(&g, out, sizeof(out));
	return k == n && memcmp(out, want, n) == 0;
}

static void qid(Qid *q, int type, unsigned vers, unsigned path) {
	q->type = (unsigned char)type;
	q->vers = vers;
	q->path = path;
	q->pathhi = 0;
}

static void the_stat(Stat9 *d) {
	memset(d, 0, sizeof(*d));
	qid(&d->qid, QTDIR, 0, 1);
	d->mode = DMDIR | 0755;
	d->atime = 10;
	d->mtime = 20;
	d->name = "/";
	d->uid = "u";
	d->gid = "g";
	d->muid = "";
}

int main(void) {
	Fcall f;
	Stat9 d, e;
	unsigned char sbuf[64], msg[80];
	static const unsigned char tversion[] = {19, 0, 0, 0, 100, 0xFF, 0xFF, 0x00, 0x20, 0, 0, 6, 0, '9', 'P', '2', '0', '0', '0'};
	static const unsigned char rversion[] = {19, 0, 0, 0, 101, 0xFF, 0xFF, 0x00, 0x20, 0, 0, 6, 0, '9', 'P', '2', '0', '0', '0'};
	static const unsigned char tauth[] = {16, 0, 0, 0, 102, 1, 0, 5, 0, 0, 0, 1, 0, 'u', 0, 0};
	static const unsigned char rauth[] = {20, 0, 0, 0, 103, 1, 0, 0, 2, 0, 0, 0, 3, 0, 0, 0, 4, 0, 0, 0};
	static const unsigned char tattach[] = {25, 0, 0, 0, 104, 2, 0, 1, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF, 6, 0, 'g', 'l', 'e', 'n', 'd', 'a', 0, 0};
	static const unsigned char rattach[] = {20, 0, 0, 0, 105, 2, 0, 0x80, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0};
	static const unsigned char rerror[] = {11, 0, 0, 0, 107, 3, 0, 2, 0, 'n', 'o'};
	static const unsigned char tflush[] = {9, 0, 0, 0, 108, 4, 0, 3, 0};
	static const unsigned char rflush[] = {7, 0, 0, 0, 109, 4, 0};
	static const unsigned char twalk[] = {24, 0, 0, 0, 110, 5, 0, 1, 0, 0, 0, 2, 0, 0, 0, 2, 0, 1, 0, 'a', 2, 0, 'b', 'c'};
	static const unsigned char rwalk[] = {35, 0, 0, 0, 111, 5, 0, 2, 0, 0x80, 0, 0, 0, 0, 7, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0};
	static const unsigned char topen[] = {12, 0, 0, 0, 112, 6, 0, 2, 0, 0, 0, 2};
	static const unsigned char ropen[] = {24, 0, 0, 0, 113, 6, 0, 0, 1, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
	static const unsigned char tcreate[] = {19, 0, 0, 0, 114, 7, 0, 2, 0, 0, 0, 1, 0, 'f', 0xA4, 0x01, 0, 0, 1};
	static const unsigned char rcreate[] = {24, 0, 0, 0, 115, 7, 0, 0, 0, 0, 0, 0, 9, 0, 0, 0, 0, 0, 0, 0, 0, 0x10, 0, 0};
	static const unsigned char tread[] = {23, 0, 0, 0, 116, 8, 0, 2, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0};
	static const unsigned char rread[] = {14, 0, 0, 0, 117, 8, 0, 3, 0, 0, 0, 'h', 'e', 'y'};
	static const unsigned char twrite[] = {25, 0, 0, 0, 118, 9, 0, 2, 0, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 'o', 'k'};
	static const unsigned char rwrite[] = {11, 0, 0, 0, 119, 9, 0, 2, 0, 0, 0};
	static const unsigned char tclunk[] = {11, 0, 0, 0, 120, 10, 0, 2, 0, 0, 0};
	static const unsigned char rclunk[] = {7, 0, 0, 0, 121, 10, 0};
	static const unsigned char tremove[] = {11, 0, 0, 0, 122, 11, 0, 3, 0, 0, 0};
	static const unsigned char rremove[] = {7, 0, 0, 0, 123, 11, 0};
	static const unsigned char tstat[] = {11, 0, 0, 0, 124, 12, 0, 1, 0, 0, 0};
	static const unsigned char rwstat[] = {7, 0, 0, 0, 127, 13, 0};

	memset(&f, 0, sizeof(f));
	f.type = Tversion;
	f.tag = NOTAG;
	f.msize = 8192;
	f.version = "9P2000";
	CHECK(both_ways(&f, tversion, sizeof(tversion)));
	f.type = Rversion;
	CHECK(both_ways(&f, rversion, sizeof(rversion)));

	memset(&f, 0, sizeof(f));
	f.type = Tauth;
	f.tag = 1;
	f.afid = 5;
	f.uname = "u";
	f.aname = "";
	CHECK(both_ways(&f, tauth, sizeof(tauth)));
	memset(&f, 0, sizeof(f));
	f.type = Rauth;
	f.tag = 1;
	qid(&f.qid, 0, 2, 3);
	f.qid.pathhi = 4;
	CHECK(both_ways(&f, rauth, sizeof(rauth)));

	memset(&f, 0, sizeof(f));
	f.type = Tattach;
	f.tag = 2;
	f.fid = 1;
	f.afid = NOFID;
	f.uname = "glenda";
	f.aname = "";
	CHECK(both_ways(&f, tattach, sizeof(tattach)));
	memcpy(copy, tattach, sizeof(tattach));
	memset(&f, 0, sizeof(f));
	CHECK(convM2S(copy, sizeof(tattach), &f) == sizeof(tattach) && f.fid == 1 && f.afid == NOFID && !strcmp(f.uname, "glenda") && !strcmp(f.aname, ""));
	memset(&f, 0, sizeof(f));
	f.type = Rattach;
	f.tag = 2;
	qid(&f.qid, QTDIR, 0, 1);
	CHECK(both_ways(&f, rattach, sizeof(rattach)));

	memset(&f, 0, sizeof(f));
	f.type = Rerror;
	f.tag = 3;
	f.ename = "no";
	CHECK(both_ways(&f, rerror, sizeof(rerror)));

	memset(&f, 0, sizeof(f));
	f.type = Tflush;
	f.tag = 4;
	f.oldtag = 3;
	CHECK(both_ways(&f, tflush, sizeof(tflush)));
	f.type = Rflush;
	CHECK(both_ways(&f, rflush, sizeof(rflush)));

	memset(&f, 0, sizeof(f));
	f.type = Twalk;
	f.tag = 5;
	f.fid = 1;
	f.newfid = 2;
	f.nwname = 2;
	f.wname[0] = "a";
	f.wname[1] = "bc";
	CHECK(both_ways(&f, twalk, sizeof(twalk)));
	memcpy(copy, twalk, sizeof(twalk));
	memset(&f, 0, sizeof(f));
	CHECK(convM2S(copy, sizeof(twalk), &f) == sizeof(twalk) && f.nwname == 2 && !strcmp(f.wname[0], "a") && !strcmp(f.wname[1], "bc") && f.newfid == 2);
	memset(&f, 0, sizeof(f));
	f.type = Rwalk;
	f.tag = 5;
	f.nwqid = 2;
	qid(&f.wqid[0], QTDIR, 0, 7);
	qid(&f.wqid[1], 0, 1, 8);
	CHECK(both_ways(&f, rwalk, sizeof(rwalk)));

	memset(&f, 0, sizeof(f));
	f.type = Topen;
	f.tag = 6;
	f.fid = 2;
	f.mode = ORDWR;
	CHECK(both_ways(&f, topen, sizeof(topen)));
	memset(&f, 0, sizeof(f));
	f.type = Ropen;
	f.tag = 6;
	qid(&f.qid, 0, 1, 8);
	CHECK(both_ways(&f, ropen, sizeof(ropen)));

	memset(&f, 0, sizeof(f));
	f.type = Tcreate;
	f.tag = 7;
	f.fid = 2;
	f.name = "f";
	f.perm = 0644;
	f.mode = OWRITE;
	CHECK(both_ways(&f, tcreate, sizeof(tcreate)));
	memset(&f, 0, sizeof(f));
	f.type = Rcreate;
	f.tag = 7;
	qid(&f.qid, 0, 0, 9);
	f.iounit = 4096;
	CHECK(both_ways(&f, rcreate, sizeof(rcreate)));

	memset(&f, 0, sizeof(f));
	f.type = Tread;
	f.tag = 8;
	f.fid = 2;
	f.offset = 0x100;
	f.count = 512;
	CHECK(both_ways(&f, tread, sizeof(tread)));
	memset(&f, 0, sizeof(f));
	f.type = Rread;
	f.tag = 8;
	f.count = 3;
	f.data = "hey";
	CHECK(both_ways(&f, rread, sizeof(rread)));

	memset(&f, 0, sizeof(f));
	f.type = Twrite;
	f.tag = 9;
	f.fid = 2;
	f.offset = 5;
	f.count = 2;
	f.data = "ok";
	CHECK(both_ways(&f, twrite, sizeof(twrite)));
	memset(&f, 0, sizeof(f));
	f.type = Rwrite;
	f.tag = 9;
	f.count = 2;
	CHECK(both_ways(&f, rwrite, sizeof(rwrite)));

	memset(&f, 0, sizeof(f));
	f.type = Tclunk;
	f.tag = 10;
	f.fid = 2;
	CHECK(both_ways(&f, tclunk, sizeof(tclunk)));
	f.type = Rclunk;
	CHECK(both_ways(&f, rclunk, sizeof(rclunk)));
	memset(&f, 0, sizeof(f));
	f.type = Tremove;
	f.tag = 11;
	f.fid = 3;
	CHECK(both_ways(&f, tremove, sizeof(tremove)));
	f.type = Rremove;
	CHECK(both_ways(&f, rremove, sizeof(rremove)));
	memset(&f, 0, sizeof(f));
	f.type = Tstat;
	f.tag = 12;
	f.fid = 1;
	CHECK(both_ways(&f, tstat, sizeof(tstat)));

	the_stat(&d);
	CHECK(sizeD2M(&d) == sizeof(statbytes));
	CHECK(convD2M(&d, sbuf, sizeof(sbuf)) == sizeof(statbytes) && !memcmp(sbuf, statbytes, sizeof(statbytes)));
	memcpy(copy, statbytes, sizeof(statbytes));
	CHECK(convM2D(copy, sizeof(statbytes), &e) == sizeof(statbytes) && e.mode == (DMDIR | 0755) && e.qid.type == QTDIR && e.qid.path == 1 && e.atime == 10 && e.mtime == 20 && !strcmp(e.name, "/") && !strcmp(e.uid, "u") && !strcmp(e.gid, "g") && !strcmp(e.muid, ""));
	CHECK(convD2M(&e, sbuf, sizeof(sbuf)) == sizeof(statbytes) && !memcmp(sbuf, statbytes, sizeof(statbytes)));

	memset(&f, 0, sizeof(f));
	f.type = Rstat;
	f.tag = 12;
	f.nstat = sizeof(statbytes);
	f.stat = (unsigned char *)statbytes;
	memcpy(msg, "\x3D\x00\x00\x00\x7D\x0C\x00\x34\x00", 9);
	memcpy(msg + 9, statbytes, sizeof(statbytes));
	CHECK(both_ways(&f, msg, 9 + sizeof(statbytes)));
	memset(&f, 0, sizeof(f));
	f.type = Twstat;
	f.tag = 13;
	f.fid = 1;
	f.nstat = sizeof(statbytes);
	f.stat = (unsigned char *)statbytes;
	memcpy(msg, "\x41\x00\x00\x00\x7E\x0D\x00\x01\x00\x00\x00\x34\x00", 13);
	memcpy(msg + 13, statbytes, sizeof(statbytes));
	CHECK(both_ways(&f, msg, 13 + sizeof(statbytes)));
	memset(&f, 0, sizeof(f));
	f.type = Rwstat;
	f.tag = 13;
	CHECK(both_ways(&f, rwstat, sizeof(rwstat)));

	memcpy(copy, twalk, sizeof(twalk));
	CHECK(convM2S(copy, sizeof(twalk) - 1, &f) == 0);
	copy[0] = 30;
	CHECK(convM2S(copy, sizeof(twalk), &f) == 0);
	memcpy(copy, rerror, sizeof(rerror));
	copy[4] = 106;
	CHECK(convM2S(copy, sizeof(rerror), &f) == 0);
	nulldir9(&d);
	CHECK(d.mode == 0xFFFFFFFFu && d.length == 0xFFFFFFFFu && !strcmp(d.name, ""));
	DONE();
}
