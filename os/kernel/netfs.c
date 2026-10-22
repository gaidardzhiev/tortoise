/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include "kernel.h"
#include "net.h"

#define I_ROOT 1
#define I_IPIFC 2
#define I_IFC0 3
#define I_IFC0CTL 4
#define I_IFC1 5
#define I_IFC1CTL 6
#define I_ROUTE 7
#define I_ARP 8
#define I_CS 9
#define I_PROTO 10
#define I_CLONE 16
#define I_CONV 0x1000
#define K_DIR 0
#define K_CTL 1
#define K_DATA 2
#define K_LOCAL 3
#define K_REMOTE 4
#define K_STATUS 5
#define K_LISTEN 6
#define K_ERR 7
#define NKIND 8
#define L_ROOT 1
#define L_ETHER 2
#define L_ETHERCTL 3
#define L_CLONE 4
#define L_CONV 0x100
#define NECONV 4
#define EQLEN 8

typedef struct Frame {
	unsigned len;
	char data[1514];
} Frame;

typedef struct EConv {
	int used, files;
	int type;
	Frame *q[EQLEN];
	int head, count;
	WaitQueue wq;
} EConv;

static Fs ip_fs, ether_fs;
static EConv econvs[NECONV];
extern FsOps netip_ops, netether_ops;

static const char *protos[] = {"tcp", "udp", "icmp"};
static const int ptypes[] = {SOCK_STREAM, SOCK_DGRAM, SOCK_RAW};
static const int pnums[] = {IPPROTO_TCP, IPPROTO_UDP, IPPROTO_ICMP};
static const char *kinds[] = {"", "ctl", "data", "local", "remote", "status", "listen", "err"};
static const char *tcp_states[] = {"Closed", "Listen", "Syn_sent", "Syn_received", "Established", "Finwait1", "Finwait2", "Close_wait", "Closing", "Last_ack", "Time_wait"};

static unsigned conv_ino(int p, int n, int k) {
	return I_CONV | ((unsigned)p << 10) | ((unsigned)n << 4) | (unsigned)k;
}

static int ino_proto(unsigned ino) {
	return (int)((ino >> 10) & 3);
}

static int ino_conv(unsigned ino) {
	return (int)((ino >> 4) & 63);
}

static int ino_kind(unsigned ino) {
	return (int)(ino & 15);
}

static Sock *conv_sock(int p, int n) {
	Sock *s;
	if (p < 0 || p > 2 || n < 0 || n >= NSOCK) return NULL;
	s = &socks[n];
	if (!s->used || s->domain != AF_INET || s->type != ptypes[p]) return NULL;
	return s;
}

static void dotted(char *out, unsigned a) {
	ksnprintf(out, 16, "%u.%u.%u.%u", a >> 24, (a >> 16) & 255, (a >> 8) & 255, a & 255);
}

static Netif *ifc(int n) {
	return n == 0 ? &lo_if : &eth_if;
}

static int parse_addr(const char *s, unsigned *addr, unsigned *port, unsigned *lport) {
	const char *p = s, *e;
	unsigned v;
	*addr = 0;
	if (lport) *lport = 0;
	if (*p == '*') {
		p++;
		if (*p != '!') return -1;
		p++;
	} else {
		const char *bang = p;
		while (*bang && *bang != '!') bang++;
		if (*bang == '!') {
			char a[20];
			unsigned n = (unsigned)(bang - p);
			if (n >= sizeof(a)) return -1;
			memcpy(a, p, n);
			a[n] = 0;
			if (ctl_addr(a, &e, addr) || *e) return -1;
			p = bang + 1;
		}
	}
	{
		char num[12];
		unsigned n = 0;
		while (p[n] && p[n] != '!' && n < 11) {
			num[n] = p[n];
			n++;
		}
		num[n] = 0;
		if (ctl_number(num, &v) || v > 65535) return -1;
		*port = v;
		p += n;
	}
	if (*p == '!') {
		if (!lport || ctl_number(p + 1, &v) || v > 65535) return -1;
		*lport = v;
	} else if (*p) return -1;
	return 0;
}

static int conv_ctl(Sock *s, const char *c) {
	unsigned a, port, lport;
	int r;
	if (!strncmp(c, "connect ", 8)) {
		if (parse_addr(c + 8, &a, &port, &lport) || (!a && s->type == SOCK_STREAM)) return set_errstr(EINVAL, "bad address", c + 8);
		if (lport && (r = inet_bind(s, 0, lport)) != 0) return r;
		r = sock_connect_inet(s, a, port);
		if (r == 0 && s->type == SOCK_STREAM) {
			while (s->tcb && s->tcb->state == TCP_SYN_SENT && !s->error) {
				r = sock_wait(s, 0);
				if (r) return r;
			}
			if (s->error) return -s->error;
		}
		return r;
	}
	if (!strncmp(c, "announce ", 9) || !strncmp(c, "bind ", 5)) {
		int ann = c[0] == 'a';
		const char *arg = c + (ann ? 9 : 5);
		char full[40];
		{
			const char *q = arg;
			int bang = 0;
			while (*q)
				if (*q++ == '!') bang = 1;
			if (!bang) {
				ksnprintf(full, sizeof(full), "*!%s", arg);
				arg = full;
			}
		}
		if (parse_addr(arg, &a, &port, NULL)) return set_errstr(EINVAL, "bad address", arg);
		if (s->type == SOCK_RAW) return set_errstr(EINVAL, "cannot announce", c);
		r = inet_bind(s, a, port);
		if (r == 0 && ann && s->type == SOCK_STREAM) r = sock_listen(s, ACCEPTMAX);
		return r;
	}
	if (!strcmp(c, "hangup")) {
		if (s->type == SOCK_STREAM && !s->tcb) return 0;
		return sock_shutdown(s, 2);
	}
	return set_errstr(EINVAL, "unknown control message", c);
}

static const char *err_text(int e) {
	switch (e) {
	case 0:
		return "";
	case ECONNREFUSED:
		return "connection refused";
	case ETIMEDOUT:
		return "connection timed out";
	case ECONNRESET:
		return "connection reset";
	case ENETUNREACH:
		return "network unreachable";
	}
	return "network error";
}

static int conv_text(int p, Sock *s, int k, char *b, unsigned max) {
	char a1[16];
	switch (k) {
	case K_CTL:
		return ksnprintf(b, max, "%d", (int)(s - socks));
	case K_LOCAL:
		dotted(a1, s->laddr);
		return ksnprintf(b, max, "%s!%u\n", a1, s->lport);
	case K_REMOTE:
		dotted(a1, s->faddr);
		return ksnprintf(b, max, "%s!%u\n", a1, s->fport);
	case K_STATUS:
		if (p == 0) {
			int st = s->tcb ? s->tcb->state : s->state == SS_LISTEN ? TCP_LISTEN : TCP_CLOSED;
			return ksnprintf(b, max, "%s\n", tcp_states[st]);
		}
		return ksnprintf(b, max, "%s\n", s->lport || s->type == SOCK_RAW ? "Open" : "Closed");
	case K_ERR:
		return ksnprintf(b, max, "%s\n", err_text(s->error ? s->error : s->neterr));
	}
	return 0;
}

static unsigned count_line(char *b, unsigned at, unsigned max, const char *s) {
	unsigned l = strlen(s);
	if (at + l >= max) l = max - at - 1;
	memcpy(b + at, s, l);
	return at + l;
}

static int cs_lookup_file(const char *path, const char *name, int field, char *out, unsigned outmax) {
	Vnode *v;
	char *buf;
	int n, i = 0;
	if (vfs_resolve(path, NULL, 1, &v)) return -1;
	buf = kmalloc(4096);
	if (!buf) {
		vput(v);
		return -1;
	}
	n = vfs_read(v, buf, 0, 4095);
	vput(v);
	if (n < 0) n = 0;
	buf[n] = 0;
	while (i < n) {
		char *line = buf + i, *w[4];
		int nw = 0, j = 0;
		while (i < n && buf[i] != '\n') i++;
		buf[i++] = 0;
		if (line[0] == '#') continue;
		while (line[j] && nw < 4) {
			while (line[j] == ' ' || line[j] == '\t') line[j++] = 0;
			if (!line[j]) break;
			w[nw++] = line + j;
			while (line[j] && line[j] != ' ' && line[j] != '\t') j++;
		}
		while (line[j]) {
			if (line[j] == ' ' || line[j] == '\t') line[j] = 0;
			j++;
		}
		for (j = 0; j < nw; j++)
			if (j != field && !strcmp(w[j], name)) {
				strncpy(out, w[field], outmax - 1);
				out[outmax - 1] = 0;
				kfree(buf);
				return 0;
			}
	}
	kfree(buf);
	return -1;
}

static int cs_query(const char *q, char *ans, unsigned max) {
	char net[8], host[64], svc[32], addr[20], port[12];
	const char *p = q, *e;
	unsigned a, i;
	for (i = 0; *p && *p != '!' && i < sizeof(net) - 1; i++) net[i] = *p++;
	net[i] = 0;
	if (*p++ != '!') return set_errstr(EINVAL, "cs: bad query", q);
	for (i = 0; *p && *p != '!' && i < sizeof(host) - 1; i++) host[i] = *p++;
	host[i] = 0;
	svc[0] = 0;
	if (*p == '!') {
		p++;
		for (i = 0; *p && i < sizeof(svc) - 1; i++) svc[i] = *p++;
		svc[i] = 0;
	}
	if (!strcmp(net, "net")) strncpy(net, "tcp", sizeof(net));
	if (strcmp(net, "tcp") && strcmp(net, "udp") && strcmp(net, "icmp")) return set_errstr(ENOENT, "cs: unknown network", net);
	if (!strcmp(host, "*")) strncpy(addr, "*", sizeof(addr));
	else if (ctl_addr(host, &e, &a) == 0 && !*e) strncpy(addr, host, sizeof(addr));
	else if (cs_lookup_file("/etc/hosts", host, 0, addr, sizeof(addr))) return set_errstr(ENOENT, "cs: can't translate address", host);
	port[0] = 0;
	if (svc[0]) {
		unsigned v;
		if (ctl_number(svc, &v) == 0) strncpy(port, svc, sizeof(port));
		else {
			char pp[24];
			char *slash;
			if (cs_lookup_file("/etc/services", svc, 1, pp, sizeof(pp))) return set_errstr(ENOENT, "cs: can't translate service", svc);
			slash = pp;
			while (*slash && *slash != '/') slash++;
			*slash = 0;
			strncpy(port, pp, sizeof(port));
		}
	}
	if (port[0]) return ksnprintf(ans, max, "/net/%s/clone %s!%s\n", net, addr, port);
	return ksnprintf(ans, max, "/net/%s/clone %s\n", net, addr);
}

static int node_type(unsigned ino) {
	if (ino == I_ROOT || ino == I_IPIFC || ino == I_IFC0 || ino == I_IFC1) return VT_DIR;
	if (ino >= I_PROTO && ino < I_PROTO + 3) return VT_DIR;
	if (ino >= I_CONV && ino_kind(ino) == K_DIR) return VT_DIR;
	return VT_REG;
}

static int ip_lookup(Vnode *dir, const char *name, Vnode **out) {
	unsigned ino = dir->ino, n, r = 0;
	int p;
	if (!strcmp(name, ".")) {
		*out = vref(dir);
		return 0;
	}
	if (!strcmp(name, "..")) {
		if (ino == I_IFC0 || ino == I_IFC1) r = I_IPIFC;
		else if (ino >= I_CONV) r = I_PROTO + (unsigned)ino_proto(ino);
		else r = I_ROOT;
	} else if (ino == I_ROOT) {
		if (!strcmp(name, "ipifc")) r = I_IPIFC;
		else if (!strcmp(name, "iproute")) r = I_ROUTE;
		else if (!strcmp(name, "arp")) r = I_ARP;
		else if (!strcmp(name, "cs")) r = I_CS;
		else
			for (p = 0; p < 3; p++)
				if (!strcmp(name, protos[p])) r = I_PROTO + (unsigned)p;
	} else if (ino == I_IPIFC) {
		if (!strcmp(name, "0")) r = I_IFC0;
		else if (!strcmp(name, "1")) r = I_IFC1;
	} else if (ino == I_IFC0 || ino == I_IFC1) {
		if (!strcmp(name, "ctl")) r = ino + 1;
	} else if (ino >= I_PROTO && ino < I_PROTO + 3) {
		p = (int)(ino - I_PROTO);
		if (!strcmp(name, "clone")) r = I_CLONE + (unsigned)p;
		else if (ctl_number(name, &n) == 0 && conv_sock(p, (int)n)) r = conv_ino(p, (int)n, K_DIR);
	} else if (ino >= I_CONV && ino_kind(ino) == K_DIR) {
		int k;
		if (!conv_sock(ino_proto(ino), ino_conv(ino))) return -ENOENT;
		for (k = 1; k < NKIND; k++)
			if (!strcmp(name, kinds[k])) r = conv_ino(ino_proto(ino), ino_conv(ino), k);
	} else return -ENOTDIR;
	if (!r) return -ENOENT;
	*out = vget(dir->fs, r, node_type(r));
	return *out ? 0 : -ENOMEM;
}

static int entry(Dirent *de, unsigned ino, const char *name) {
	de->inode = ino;
	strncpy(de->name, name, 28);
	return 1;
}

static int ip_readdir(Vnode *v, unsigned *off, Dirent *de) {
	static const char *rootnames[] = {"ipifc", "iproute", "arp", "cs", "tcp", "udp", "icmp"};
	static const unsigned rootinos[] = {I_IPIFC, I_ROUTE, I_ARP, I_CS, I_PROTO, I_PROTO + 1, I_PROTO + 2};
	unsigned ino = v->ino, k = *off;
	if (k < 2) {
		(*off)++;
		return entry(de, ino, k ? ".." : ".");
	}
	k -= 2;
	if (ino == I_ROOT) {
		if (k >= 7) return 0;
		(*off)++;
		return entry(de, rootinos[k], rootnames[k]);
	}
	if (ino == I_IPIFC) {
		if (k >= 2) return 0;
		(*off)++;
		return entry(de, k ? I_IFC1 : I_IFC0, k ? "1" : "0");
	}
	if (ino == I_IFC0 || ino == I_IFC1) {
		if (k >= 1) return 0;
		(*off)++;
		return entry(de, ino + 1, "ctl");
	}
	if (ino >= I_PROTO && ino < I_PROTO + 3) {
		int p = (int)(ino - I_PROTO);
		if (k == 0) {
			(*off)++;
			return entry(de, I_CLONE + (unsigned)p, "clone");
		}
		while (k - 1 < NSOCK) {
			int n = (int)(k - 1);
			(*off)++;
			k++;
			if (conv_sock(p, n)) {
				char num[8];
				ksnprintf(num, sizeof(num), "%d", n);
				return entry(de, conv_ino(p, n, K_DIR), num);
			}
		}
		return 0;
	}
	if (ino >= I_CONV && ino_kind(ino) == K_DIR) {
		if (k + 1 >= NKIND || !conv_sock(ino_proto(ino), ino_conv(ino))) return 0;
		(*off)++;
		return entry(de, conv_ino(ino_proto(ino), ino_conv(ino), (int)k + 1), kinds[k + 1]);
	}
	return -ENOTDIR;
}

static int ip_getattr(Vnode *v, Stat2 *st) {
	unsigned ino = v->ino;
	if (ino >= I_CONV && !conv_sock(ino_proto(ino), ino_conv(ino))) return -ENOENT;
	st->dev = v->fs->dev;
	st->ino = ino;
	st->atime = st->mtime = st->ctime = vfs_now();
	if (v->type == VT_DIR) {
		st->mode = S_IFDIR | 0555;
		st->nlink = 2;
		return 0;
	}
	st->nlink = 1;
	if (ino == I_IFC0CTL || ino == I_IFC1CTL || ino == I_ROUTE) st->mode = S_IFREG | 0644;
	else if (ino == I_ARP) st->mode = S_IFREG | 0444;
	else st->mode = S_IFREG | 0666;
	return 0;
}

static int ip_text(Vnode *v, char *b, unsigned max, File *f) {
	unsigned ino = v->ino;
	char a1[16], a2[16];
	if (ino == I_IFC0CTL || ino == I_IFC1CTL) {
		Netif *n = ifc(ino == I_IFC0CTL ? 0 : 1);
		dotted(a1, n->addr);
		dotted(a2, n->mask);
		return ksnprintf(b, max, "%s %s %s %s\n", n->name, a1, a2, (n->flags & IFF_UP) ? "up" : "down");
	}
	if (ino == I_ROUTE) return net_proc(5, b, max);
	if (ino == I_ARP) return net_proc(4, b, max);
	if (ino == I_CS) {
		if (!f || !f->snap) return 0;
		memcpy(b, f->snap, (unsigned)f->snaplen);
		return f->snaplen;
	}
	if (ino >= I_CONV) {
		Sock *s = conv_sock(ino_proto(ino), ino_conv(ino));
		if (!s) return -ENOENT;
		return conv_text(ino_proto(ino), s, ino_kind(ino), b, max);
	}
	return 0;
}

static int ip_read(Vnode *v, char *buf, unsigned off, unsigned n, File *f) {
	char *b;
	int len;
	if (v->type == VT_DIR) return -EISDIR;
	b = kmalloc(4096);
	if (!b) return -ENOMEM;
	len = ip_text(v, b, 4096, f);
	if (len < 0) {
		kfree(b);
		return len;
	}
	if (off >= (unsigned)len) n = 0;
	else if (n > (unsigned)len - off) n = (unsigned)len - off;
	memcpy(buf, b + off, n);
	kfree(b);
	return (int)n;
}

static int ifc_ctl(Netif *n, const char *c) {
	unsigned a, m;
	const char *p;
	int r;
	if (!strcmp(c, "up") || !strcmp(c, "down")) return netif_set(n, SIOCSIFFLAGS, (c[0] == 'u' ? IFF_UP : 0) | (n->flags & IFF_PROMISC));
	if (!strncmp(c, "add ", 4) || !strncmp(c, "remove ", 7)) {
		int add = c[0] == 'a';
		p = c + (add ? 4 : 7);
		if (ctl_addr(p, &p, &a) || ctl_addr(p, &p, &m) || *p) return set_errstr(EINVAL, "bad address in control message", c);
		if (add) {
			r = netif_set(n, SIOCSIFADDR, a);
			return r ? r : netif_set(n, SIOCSIFNETMASK, m);
		}
		if (n->addr != a || n->mask != m) return set_errstr(EINVAL, "address not on interface", c);
		r = netif_set(n, SIOCSIFADDR, 0);
		return r ? r : netif_set(n, SIOCSIFNETMASK, 0);
	}
	return set_errstr(EINVAL, "unknown control message", c);
}

static int route_ctl(const char *c) {
	unsigned d, m, g = 0;
	const char *p;
	if (!strncmp(c, "add ", 4)) {
		p = c + 4;
		if (ctl_addr(p, &p, &d) || ctl_addr(p, &p, &m) || ctl_addr(p, &p, &g) || *p) return set_errstr(EINVAL, "bad address in control message", c);
		return route_set(0, d, m, g, NULL);
	}
	if (!strncmp(c, "remove ", 7)) {
		p = c + 7;
		if (ctl_addr(p, &p, &d) || ctl_addr(p, &p, &m) || *p) return set_errstr(EINVAL, "bad address in control message", c);
		return route_set(1, d, m, 0, NULL);
	}
	return set_errstr(EINVAL, "unknown control message", c);
}

static int ip_write(Vnode *v, const char *buf, unsigned off, unsigned n, File *f) {
	unsigned ino = v->ino;
	char cmd[ERRMAX];
	int r;
	(void)off;
	if (v->type == VT_DIR) return -EISDIR;
	r = ctl_text(cmd, buf, n);
	if (r < 0) return r;
	if (ino == I_IFC0CTL || ino == I_IFC1CTL) r = ifc_ctl(ifc(ino == I_IFC0CTL ? 0 : 1), cmd);
	else if (ino == I_ROUTE) r = route_ctl(cmd);
	else if (ino == I_CS) {
		char ans[128];
		r = cs_query(cmd, ans, sizeof(ans));
		if (r > 0 && f) {
			kfree(f->snap);
			f->snap = kmalloc((unsigned)r);
			f->snaplen = f->snap ? r : 0;
			if (f->snap) memcpy(f->snap, ans, (unsigned)r);
			f->off = 0;
		}
	} else if (ino >= I_CONV && ino_kind(ino) == K_CTL) {
		Sock *s = conv_sock(ino_proto(ino), ino_conv(ino));
		r = s ? conv_ctl(s, cmd) : -ENOENT;
		if (s && r < 0) s->neterr = -r;
	} else r = -EACCES;
	return r < 0 ? r : (int)n;
}

static int ip_truncate(Vnode *v, unsigned len) {
	(void)v;
	(void)len;
	return 0;
}

static int ip_poll(Vnode *v, File *f) {
	(void)v;
	(void)f;
	return POLLIN | POLLOUT;
}

FsOps netip_ops = {
	ip_lookup, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	ip_read, ip_write, ip_readdir, ip_getattr, NULL, ip_truncate, NULL, ip_poll, NULL, NULL
};

static void conv_path(File *f, const char *proto, int n, const char *kind) {
	char *b = kmalloc(MAXPATH);
	if (!b) return;
	ksnprintf(b, MAXPATH, "/net/%s/%d/%s", proto, n, kind);
	kfree(f->path);
	f->path = b;
}

static int become_ctl(File *f, int p, Sock *s) {
	Vnode *c = vget(&ip_fs, conv_ino(p, (int)(s - socks), K_CTL), VT_REG);
	if (!c) return -ENOMEM;
	vput(f->vn);
	f->vn = c;
	f->sock = s;
	s->files++;
	conv_path(f, protos[p], (int)(s - socks), "ctl");
	return 0;
}

static int ip_open(Vnode *v, File *f) {
	unsigned ino = v->ino;
	int p, k, err;
	Sock *s, *n;
	if (ino >= I_CLONE && ino < I_CLONE + 3) {
		p = (int)(ino - I_CLONE);
		s = sock_new(AF_INET, ptypes[p], pnums[p], &err);
		if (!s) return err;
		return become_ctl(f, p, s);
	}
	if (ino < I_CONV) return 0;
	p = ino_proto(ino);
	k = ino_kind(ino);
	s = conv_sock(p, ino_conv(ino));
	if (!s) return -ENOENT;
	if (k == K_CTL) {
		f->sock = s;
		s->files++;
	} else if (k == K_DATA) {
		vput(f->vn);
		f->vn = NULL;
		f->type = FD_SOCKET;
		f->sock = s;
		s->files++;
	} else if (k == K_LISTEN) {
		n = sock_accept(s, f->nonblock, &err);
		if (!n) return err;
		return become_ctl(f, p, n);
	}
	return 0;
}

static EConv *econv_of(unsigned ino) {
	unsigned n = (ino >> 4) & 15;
	return ino >= L_CONV && n < NECONV && econvs[n].used ? &econvs[n] : NULL;
}

static unsigned eino(int n, int k) {
	return L_CONV | ((unsigned)n << 4) | (unsigned)k;
}

static int l_lookup(Vnode *dir, const char *name, Vnode **out) {
	unsigned ino = dir->ino, r = 0, n;
	if (!strcmp(name, ".")) {
		*out = vref(dir);
		return 0;
	}
	if (!strcmp(name, "..")) r = ino >= L_CONV ? L_ETHER : L_ROOT;
	else if (ino == L_ROOT) {
		if (!strcmp(name, "ether0")) r = L_ETHER;
	} else if (ino == L_ETHER) {
		if (!strcmp(name, "ctl")) r = L_ETHERCTL;
		else if (!strcmp(name, "clone")) r = L_CLONE;
		else if (ctl_number(name, &n) == 0 && n < NECONV && econvs[n].used) r = eino((int)n, 0);
	} else if (ino >= L_CONV && (ino & 15) == 0) {
		if (!econv_of(ino)) return -ENOENT;
		if (!strcmp(name, "ctl")) r = ino | 1;
		else if (!strcmp(name, "data")) r = ino | 2;
	} else return -ENOTDIR;
	if (!r) return -ENOENT;
	*out = vget(dir->fs, r, (r == L_ROOT || r == L_ETHER || (r >= L_CONV && (r & 15) == 0)) ? VT_DIR : VT_REG);
	return *out ? 0 : -ENOMEM;
}

static int l_readdir(Vnode *v, unsigned *off, Dirent *de) {
	unsigned ino = v->ino, k = *off;
	if (k < 2) {
		(*off)++;
		return entry(de, ino, k ? ".." : ".");
	}
	k -= 2;
	if (ino == L_ROOT) {
		if (k) return 0;
		(*off)++;
		return entry(de, L_ETHER, "ether0");
	}
	if (ino == L_ETHER) {
		if (k == 0 || k == 1) {
			(*off)++;
			return entry(de, k ? L_CLONE : L_ETHERCTL, k ? "clone" : "ctl");
		}
		while (k - 2 < NECONV) {
			int n = (int)(k - 2);
			(*off)++;
			k++;
			if (econvs[n].used) {
				char num[4];
				ksnprintf(num, sizeof(num), "%d", n);
				return entry(de, eino(n, 0), num);
			}
		}
		return 0;
	}
	if (ino >= L_CONV && (ino & 15) == 0) {
		if (k >= 2 || !econv_of(ino)) return 0;
		(*off)++;
		return entry(de, ino | (k + 1), k ? "data" : "ctl");
	}
	return -ENOTDIR;
}

static int l_getattr(Vnode *v, Stat2 *st) {
	if (v->ino >= L_CONV && !econv_of(v->ino)) return -ENOENT;
	st->dev = v->fs->dev;
	st->ino = v->ino;
	st->atime = st->mtime = st->ctime = vfs_now();
	st->nlink = v->type == VT_DIR ? 2 : 1;
	st->mode = v->type == VT_DIR ? S_IFDIR | 0555 : v->ino == L_ETHERCTL ? S_IFREG | 0644 : S_IFREG | 0660;
	return 0;
}

static int ether_ctl(const char *c) {
	unsigned up = eth_if.flags & IFF_UP, pr = eth_if.flags & IFF_PROMISC;
	if (!strcmp(c, "up")) up = IFF_UP;
	else if (!strcmp(c, "down")) up = 0;
	else if (!strcmp(c, "promiscuous on")) pr = IFF_PROMISC;
	else if (!strcmp(c, "promiscuous off")) pr = 0;
	else return set_errstr(EINVAL, "unknown control message", c);
	return netif_set(&eth_if, SIOCSIFFLAGS, up | pr);
}

static int l_read(Vnode *v, char *buf, unsigned off, unsigned n, File *f) {
	EConv *e;
	char t[32];
	int len;
	(void)f;
	if (v->type == VT_DIR) return -EISDIR;
	if (v->ino == L_ETHERCTL || (v->ino >= L_CONV && (v->ino & 15) == 1)) {
		if (v->ino == L_ETHERCTL) len = ksnprintf(t, sizeof(t), "%02x%02x%02x%02x%02x%02x\n", eth_if.mac[0], eth_if.mac[1], eth_if.mac[2], eth_if.mac[3], eth_if.mac[4], eth_if.mac[5]);
		else len = ksnprintf(t, sizeof(t), "%u", (v->ino >> 4) & 15);
		if (off >= (unsigned)len) return 0;
		if (n > (unsigned)len - off) n = (unsigned)len - off;
		memcpy(buf, t + off, n);
		return (int)n;
	}
	e = econv_of(v->ino);
	if (!e) return -ENOENT;
	while (e->count == 0) {
		if (f && f->nonblock) return -EAGAIN;
		if (signal_pending()) return -EINTR;
		sleep_on(&e->wq);
		if (signal_pending() && e->count == 0) return -EINTR;
	}
	{
		Frame *fr = e->q[e->head];
		unsigned k = fr->len < n ? fr->len : n;
		memcpy(buf, fr->data, k);
		kfree(fr);
		e->head = (e->head + 1) % EQLEN;
		e->count--;
		return (int)k;
	}
}

static int l_write(Vnode *v, const char *buf, unsigned off, unsigned n, File *f) {
	EConv *e;
	char cmd[ERRMAX];
	int r;
	(void)off;
	(void)f;
	if (v->ino == L_ETHERCTL) {
		r = ctl_text(cmd, buf, n);
		if (r < 0) return r;
		r = ether_ctl(cmd);
		return r < 0 ? r : (int)n;
	}
	e = econv_of(v->ino);
	if (!e) return -ENOENT;
	if ((v->ino & 15) == 1) {
		unsigned t;
		r = ctl_text(cmd, buf, n);
		if (r < 0) return r;
		if (strncmp(cmd, "connect ", 8)) return set_errstr(EINVAL, "unknown control message", cmd);
		if (!strcmp(cmd + 8, "-1")) e->type = -1;
		else if (ctl_number(cmd + 8, &t) == 0 && t < 65536) e->type = (int)t;
		else return set_errstr(EINVAL, "bad frame type", cmd + 8);
		return (int)n;
	}
	if (curproc->euid) return -EPERM;
	if (n < 14) return set_errstr(EINVAL, "short frame", NULL);
	r = ether_send((const unsigned char *)buf, ((unsigned)(unsigned char)buf[12] << 8) | (unsigned char)buf[13], buf + 14, n - 14);
	return r < 0 ? r : (int)n;
}

static int l_poll(Vnode *v, File *f) {
	EConv *e = econv_of(v->ino);
	(void)f;
	if (e && (v->ino & 15) == 2) return (e->count ? POLLIN : 0) | POLLOUT;
	return POLLIN | POLLOUT;
}

FsOps netether_ops = {
	l_lookup, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	l_read, l_write, l_readdir, l_getattr, NULL, ip_truncate, NULL, l_poll, NULL, NULL
};

static int l_open(Vnode *v, File *f) {
	EConv *e;
	int i;
	if (v->ino == L_CLONE) {
		Vnode *c;
		if (curproc->euid) return -EPERM;
		for (i = 0; i < NECONV; i++)
			if (!econvs[i].used) break;
		if (i == NECONV) return -ENOSPC;
		memset(&econvs[i], 0, sizeof(EConv));
		econvs[i].used = 1;
		econvs[i].type = 0;
		c = vget(&ether_fs, eino(i, 1), VT_REG);
		if (!c) {
			econvs[i].used = 0;
			return -ENOMEM;
		}
		vput(f->vn);
		f->vn = c;
		econvs[i].files = 1;
		{
			char *b = kmalloc(MAXPATH);
			if (b) {
				ksnprintf(b, MAXPATH, "/net/ether0/%d/ctl", i);
				kfree(f->path);
				f->path = b;
			}
		}
		return 0;
	}
	e = econv_of(v->ino);
	if (e && (v->ino & 15)) e->files++;
	return 0;
}

int net9_open(Vnode *v, File *f) {
	if (v->fs == &ip_fs) return ip_open(v, f);
	if (v->fs == &ether_fs) return l_open(v, f);
	return 0;
}

void net9_close(File *f) {
	EConv *e;
	int i;
	if (f->type != FD_INODE || !f->vn || f->vn->fs != &ether_fs) return;
	e = econv_of(f->vn->ino);
	if (!e || !(f->vn->ino & 15) || --e->files > 0) return;
	for (i = 0; i < e->count; i++) kfree(e->q[(e->head + i) % EQLEN]);
	e->used = 0;
	wakeup(&e->wq);
}

void ether_deliver(const char *frame, unsigned len) {
	unsigned type = ((unsigned)(unsigned char)frame[12] << 8) | (unsigned char)frame[13];
	int i;
	for (i = 0; i < NECONV; i++) {
		EConv *e = &econvs[i];
		Frame *fr;
		if (!e->used || (e->type != -1 && (unsigned)e->type != type) || e->count == EQLEN) continue;
		fr = kmalloc(sizeof(Frame));
		if (!fr) continue;
		fr->len = len > sizeof(fr->data) ? sizeof(fr->data) : len;
		memcpy(fr->data, frame, fr->len);
		e->q[(e->head + e->count) % EQLEN] = fr;
		e->count++;
		wakeup(&e->wq);
		wakeup(&poll_wq);
	}
}

void netfs_init(void) {
	memset(&ip_fs, 0, sizeof(Fs));
	ip_fs.ops = &netip_ops;
	ip_fs.dev = (44u << 8);
	strncpy(ip_fs.source, "#I", 63);
	strncpy(ip_fs.fstype, "ip", 7);
	ip_fs.root = vget(&ip_fs, I_ROOT, VT_DIR);
	memset(&ether_fs, 0, sizeof(Fs));
	ether_fs.ops = &netether_ops;
	ether_fs.dev = (45u << 8);
	strncpy(ether_fs.source, "#l", 63);
	strncpy(ether_fs.fstype, "ether", 7);
	ether_fs.root = vget(&ether_fs, L_ROOT, VT_DIR);
}

Vnode *netfs_root(int c) {
	return vref(c == 'I' ? ip_fs.root : ether_fs.root);
}
