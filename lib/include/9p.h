/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#ifndef _9P_H
#define _9P_H
#define VERSION9P "9P2000"
#define MAXMSG9P 8192
#define IOHDRSZ 24
#define MAXWELEM 16
#define NOTAG 0xFFFF
#define NOFID 0xFFFFFFFFu
#define Tversion 100
#define Rversion 101
#define Tauth 102
#define Rauth 103
#define Tattach 104
#define Rattach 105
#define Rerror 107
#define Tflush 108
#define Rflush 109
#define Twalk 110
#define Rwalk 111
#define Topen 112
#define Ropen 113
#define Tcreate 114
#define Rcreate 115
#define Tread 116
#define Rread 117
#define Twrite 118
#define Rwrite 119
#define Tclunk 120
#define Rclunk 121
#define Tremove 122
#define Rremove 123
#define Tstat 124
#define Rstat 125
#define Twstat 126
#define Rwstat 127
#define QTDIR 0x80
#define QTSYMLINK 0x02
#define QTFILE 0x00
#define DMDIR 0x80000000u
#define DMSYMLINK 0x02000000u
#define OREAD 0
#define OWRITE 1
#define ORDWR 2
#define OEXEC 3
#define OTRUNC 0x10
#define ORCLOSE 0x40

typedef struct Qid {
	unsigned char type;
	unsigned vers;
	unsigned path, pathhi;
} Qid;

typedef struct Stat9 {
	unsigned short type;
	unsigned dev;
	Qid qid;
	unsigned mode, atime, mtime;
	unsigned length, lengthhi;
	char *name, *uid, *gid, *muid;
} Stat9;

typedef struct Fcall {
	unsigned char type;
	unsigned short tag;
	unsigned fid, afid, newfid;
	unsigned msize, iounit, count, perm;
	unsigned short oldtag;
	unsigned char mode;
	unsigned offset, offsethi;
	char *version, *uname, *aname, *ename, *name;
	int nwname;
	char *wname[MAXWELEM];
	int nwqid;
	Qid wqid[MAXWELEM];
	Qid qid;
	unsigned nstat;
	unsigned char *stat;
	char *data;
} Fcall;

unsigned sizeS2M(Fcall *f);
unsigned convS2M(Fcall *f, unsigned char *buf, unsigned n);
unsigned convM2S(unsigned char *buf, unsigned n, Fcall *f);
unsigned sizeD2M(Stat9 *d);
unsigned convD2M(Stat9 *d, unsigned char *buf, unsigned n);
unsigned convM2D(unsigned char *buf, unsigned n, Stat9 *d);
void nulldir9(Stat9 *d);

typedef struct Fid9 {
	unsigned fid;
	int used, omode;
	Qid qid;
	void *aux;
	unsigned diroff, dirindex;
} Fid9;

typedef struct Srv9 Srv9;
struct Srv9 {
	const char *(*attach)(Srv9 *s, Fid9 *f, Fcall *in, Fcall *out);
	const char *(*walk1)(Srv9 *s, Fid9 *f, const char *name, Qid *q);
	const char *(*clone)(Srv9 *s, Fid9 *old, Fid9 *f);
	const char *(*open)(Srv9 *s, Fid9 *f, int mode, Fcall *out);
	const char *(*create)(Srv9 *s, Fid9 *f, const char *name, unsigned perm, int mode, Fcall *out);
	const char *(*read)(Srv9 *s, Fid9 *f, Fcall *in, Fcall *out);
	const char *(*write)(Srv9 *s, Fid9 *f, Fcall *in, Fcall *out);
	const char *(*remove)(Srv9 *s, Fid9 *f);
	const char *(*stat)(Srv9 *s, Fid9 *f, Stat9 *d);
	const char *(*wstat)(Srv9 *s, Fid9 *f, Stat9 *d);
	void (*clunk)(Srv9 *s, Fid9 *f);
	const char *(*dirent)(Srv9 *s, Fid9 *f, unsigned index, Stat9 *d);
	int infd, outfd;
	unsigned msize;
	void *aux;
	Fid9 *fids;
	int nfids;
	int debug;
};

int serve9(Srv9 *s);
const char *readbuf9(Fcall *in, Fcall *out, const char *src, unsigned len);
#endif
