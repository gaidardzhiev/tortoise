/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include "kernel.h"
#include "net.h"

Sock socks[NSOCK];
static int next_sock_id = 1;

unsigned short htons_k(unsigned short v) {
	return (unsigned short)((v >> 8) | (v << 8));
}

unsigned htonl_k(unsigned v) {
	return (v >> 24) | ((v >> 8) & 0xFF00) | ((v << 8) & 0xFF0000) | (v << 24);
}

unsigned csum_add(const void *p, unsigned n, unsigned sum) {
	const unsigned char *b = p;
	unsigned i;
	for (i = 0; i + 1 < n; i += 2) sum += (unsigned)(b[i] << 8 | b[i + 1]);
	if (n & 1) sum += (unsigned)(b[n - 1] << 8);
	return sum;
}

unsigned short csum_fold(unsigned sum) {
	while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
	return (unsigned short)~sum;
}

unsigned short csum(const void *p, unsigned n, unsigned start) {
	return csum_fold(csum_add(p, n, start));
}

int ring_init(Ring *r) {
	if (r->buf) return 0;
	r->buf = (char *)low_alloc_n(RINGSIZE / PAGE);
	if (!r->buf) return -ENOBUFS;
	r->size = RINGSIZE;
	r->head = r->len = 0;
	return 0;
}

void ring_free(Ring *r) {
	if (r->buf) low_free_n((unsigned)r->buf, RINGSIZE / PAGE);
	r->buf = NULL;
	r->len = r->head = 0;
}

unsigned ring_space(Ring *r) {
	return r->buf ? r->size - r->len : 0;
}

unsigned ring_put(Ring *r, const char *p, unsigned n) {
	unsigned i, k = ring_space(r);
	if (n > k) n = k;
	for (i = 0; i < n; i++) r->buf[(r->head + r->len + i) % r->size] = p[i];
	r->len += n;
	return n;
}

void ring_peek_at(Ring *r, unsigned off, char *p, unsigned n) {
	unsigned i;
	for (i = 0; i < n; i++) p[i] = r->buf[(r->head + off + i) % r->size];
}

void ring_drop(Ring *r, unsigned n) {
	if (n > r->len) n = r->len;
	r->head = (r->head + n) % (r->size ? r->size : 1);
	r->len -= n;
}

unsigned ring_get(Ring *r, char *p, unsigned n, int peek) {
	if (n > r->len) n = r->len;
	ring_peek_at(r, 0, p, n);
	if (!peek) ring_drop(r, n);
	return n;
}

void sock_wake(Sock *s) {
	wakeup(&s->wq);
	wakeup(&poll_wq);
}

Sock *sock_alloc(int domain, int type, int proto) {
	int i;
	for (i = 0; i < NSOCK; i++)
		if (!socks[i].used) {
			Sock *s = &socks[i];
			memset(s, 0, sizeof(Sock));
			s->used = 1;
			s->id = next_sock_id++;
			s->domain = domain;
			s->type = type;
			s->proto = proto;
			s->state = SS_UNCONN;
			s->pid = curproc ? curproc->pid : 0;
			return s;
		}
	return NULL;
}

void sock_free(Sock *s) {
	Dgram *d = s->dq;
	while (d) {
		Dgram *n = d->next;
		kfree(d);
		d = n;
	}
	ring_free(&s->rx);
	ring_free(&s->tx);
	if (s->bound_vn) vput(s->bound_vn);
	memset(s, 0, sizeof(Sock));
}

static void sock_timeout(void *arg) {
	Proc *p = arg;
	p->timer_fired = 1;
	if (p->state == P_SLEEPING) {
		p->state = P_RUNNABLE;
		if (p->chan) {
			WaitQueue *wq = p->chan;
			if (wq->waiters > 0) wq->waiters--;
			p->chan = NULL;
		}
	}
}

int sock_wait(Sock *s, int nonblock) {
	int id = 0;
	if (net_run() > 0) return signal_pending() ? -EINTR : 0;
	if (nonblock) return -EAGAIN;
	if (signal_pending()) return -EINTR;
	curproc->timer_fired = 0;
	if (s->rcvtimeo) id = timer_add(s->rcvtimeo, sock_timeout, curproc);
	sleep_on(&s->wq);
	if (id && !curproc->timer_fired) timer_cancel(id);
	if (curproc->timer_fired && s->rcvtimeo) return -EAGAIN;
	if (signal_pending()) return -EINTR;
	return 0;
}

unsigned sock_id(File *f) {
	return f->sock ? (unsigned)((Sock *)f->sock)->id : 0;
}

static Sock *fd_sock(int fd, int *err) {
	File *f = getf(fd);
	if (!f) {
		*err = -EBADF;
		return NULL;
	}
	if (f->type != FD_SOCKET) {
		*err = -ENOTSOCK;
		return NULL;
	}
	((Sock *)f->sock)->nonblock = f->nonblock;
	return f->sock;
}

static int sock_fd(Sock *s) {
	File *f = file_alloc();
	int fd;
	if (!f) return -ENFILE;
	f->type = FD_SOCKET;
	f->readable = f->writable = 1;
	f->sock = s;
	s->files++;
	fd = fd_alloc(f);
	if (fd < 0) file_close(f);
	return fd;
}

static Sock *unix_lookup(const char *path, int type, int *err) {
	Vnode *v;
	int i, r = vfs_resolve(path, curproc->cwd, 1, &v);
	if (r) {
		*err = r == -ENOENT ? -ECONNREFUSED : r;
		return NULL;
	}
	if (v->type != VT_SOCK) {
		vput(v);
		*err = -ECONNREFUSED;
		return NULL;
	}
	for (i = 0; i < NSOCK; i++)
		if (socks[i].used && socks[i].domain == AF_UNIX && socks[i].bound_vn == v) {
			vput(v);
			if (socks[i].type != type) {
				*err = -EPROTOTYPE;
				return NULL;
			}
			return &socks[i];
		}
	vput(v);
	*err = -ECONNREFUSED;
	return NULL;
}

static int read_addr(unsigned uaddr, int *family, unsigned *addr, unsigned *port, char *path) {
	unsigned short fam;
	if (!uaddr) return -EFAULT;
	if (copyin(&fam, uaddr, 2)) return -EFAULT;
	*family = fam;
	if (fam == AF_INET) {
		unsigned char b[8];
		if (copyin(b, uaddr, 8)) return -EFAULT;
		*port = (unsigned)(b[2] << 8 | b[3]);
		*addr = (unsigned)b[4] << 24 | (unsigned)b[5] << 16 | (unsigned)b[6] << 8 | b[7];
		return 0;
	}
	if (fam == AF_UNIX) {
		int r = copyinstr(path, uaddr + 2, 108);
		if (r < 0) return r == -EINVAL ? -ENAMETOOLONG : r;
		if (!path[0]) return -EINVAL;
		return 0;
	}
	return -EAFNOSUPPORT;
}

static int write_addr(unsigned uaddr, int family, unsigned addr, unsigned port, const char *path) {
	unsigned char b[110];
	if (!uaddr) return 0;
	memset(b, 0, sizeof(b));
	b[0] = (unsigned char)family;
	if (family == AF_INET) {
		b[2] = (unsigned char)(port >> 8);
		b[3] = (unsigned char)port;
		b[4] = (unsigned char)(addr >> 24);
		b[5] = (unsigned char)(addr >> 16);
		b[6] = (unsigned char)(addr >> 8);
		b[7] = (unsigned char)addr;
		return copyout(uaddr, b, 16) ? -EFAULT : 0;
	}
	strncpy((char *)b + 2, path ? path : "", 107);
	return copyout(uaddr, b, 110) ? -EFAULT : 0;
}

Sock *sock_new(int domain, int type, int proto, int *err) {
	Sock *s;
	*err = 0;
	if (domain != AF_UNIX && domain != AF_INET) *err = -EAFNOSUPPORT;
	else if (type != SOCK_STREAM && type != SOCK_DGRAM && type != SOCK_RAW) *err = -EINVAL;
	else if (domain == AF_UNIX && type == SOCK_RAW) *err = -EPROTONOSUPPORT;
	else if (type == SOCK_RAW) {
		if (proto != IPPROTO_ICMP) *err = -EPROTONOSUPPORT;
		else if (curproc->euid) *err = -EPERM;
	} else if (domain == AF_INET && proto && proto != (type == SOCK_STREAM ? IPPROTO_TCP : IPPROTO_UDP)) {
		*err = -EPROTONOSUPPORT;
	}
	if (*err) return NULL;
	s = sock_alloc(domain, type, proto);
	if (!s) *err = -ENFILE;
	return s;
}

int sys_socket(int domain, int type, int proto) {
	Sock *s;
	int fd, err;
	s = sock_new(domain, type, proto, &err);
	if (!s) return err;
	fd = sock_fd(s);
	if (fd < 0) return fd;
	return fd;
}

static int unix_bind(Sock *s, const char *path) {
	char name[28];
	Vnode *dir, *v;
	int r;
	if (s->bound_vn) return -EINVAL;
	r = vfs_parent(path, curproc->cwd, &dir, name);
	if (r) return r;
	if (!dir->fs->ops->mknod) {
		vput(dir);
		return -EOPNOTSUPP;
	}
	r = vfs_access(dir, 3);
	if (r == 0 && dir->fs->ops->lookup(dir, name, &v) == 0) {
		vput(v);
		r = -EADDRINUSE;
	}
	if (r == 0) r = dir->fs->ops->mknod(dir, name, S_IFSOCK | (0777 & ~curproc->umask), &v);
	vput(dir);
	if (r) return r;
	s->bound_vn = v;
	strncpy(s->path, path, 107);
	return 0;
}

int sys_bind(int fd, unsigned uaddr) {
	int err = 0, family, r;
	unsigned addr, port;
	char path[108];
	Sock *s = fd_sock(fd, &err);
	if (!s) return err;
	r = read_addr(uaddr, &family, &addr, &port, path);
	if (r) return r;
	if (family != s->domain) return -EAFNOSUPPORT;
	if (family == AF_UNIX) return unix_bind(s, path);
	return inet_bind(s, addr, port);
}

int sys_listen(int fd, int backlog) {
	int err = 0;
	Sock *s = fd_sock(fd, &err);
	if (!s) return err;
	return sock_listen(s, backlog);
}

int sock_listen(Sock *s, int backlog) {
	if (s->type != SOCK_STREAM) return -EOPNOTSUPP;
	if (backlog < 1) backlog = 1;
	if (backlog > ACCEPTMAX) backlog = ACCEPTMAX;
	if (s->domain == AF_INET) return tcp_listen(s, backlog);
	if (!s->bound_vn) return -EINVAL;
	s->state = SS_LISTEN;
	s->backlog = backlog;
	return 0;
}

static int unix_connect_stream(Sock *s, const char *path) {
	int err = 0;
	Sock *l = unix_lookup(path, SOCK_STREAM, &err), *n;
	if (!l) return err;
	if (l->state != SS_LISTEN || l->nacc >= l->backlog) return -ECONNREFUSED;
	if (s->state == SS_CONNECTED) return -EISCONN;
	n = sock_alloc(AF_UNIX, SOCK_STREAM, 0);
	if (!n) return -ENFILE;
	if (ring_init(&n->rx) || ring_init(&s->rx)) {
		sock_free(n);
		return -ENOBUFS;
	}
	strncpy(n->path, l->path, 107);
	strncpy(s->peer_path, l->path, 107);
	n->state = s->state = SS_CONNECTED;
	n->peer = s;
	s->peer = n;
	n->parent = l;
	l->acceptq[l->nacc++] = n;
	sock_wake(l);
	return 0;
}

int sys_connect(int fd, unsigned uaddr) {
	int err = 0, family, r;
	unsigned addr, port;
	char path[108];
	Sock *s = fd_sock(fd, &err);
	if (!s) return err;
	r = read_addr(uaddr, &family, &addr, &port, path);
	if (r) return r;
	if (family != s->domain) return -EAFNOSUPPORT;
	if (family == AF_UNIX) {
		if (s->type == SOCK_STREAM) return unix_connect_stream(s, path);
		if (!unix_lookup(path, SOCK_DGRAM, &err)) return err;
		strncpy(s->peer_path, path, 107);
		s->state = SS_CONNECTED;
		return 0;
	}
	return sock_connect_inet(s, addr, port);
}

int sock_connect_inet(Sock *s, unsigned addr, unsigned port) {
	int r;
	if (s->type == SOCK_STREAM) {
		r = tcp_connect(s, addr, port);
		net_run();
		return r;
	}
	if (!s->lport) {
		r = inet_bind(s, 0, 0);
		if (r) return r;
	}
	s->faddr = addr;
	s->fport = (unsigned short)port;
	s->state = SS_CONNECTED;
	return 0;
}

Sock *sock_accept(Sock *s, int nonblock, int *err) {
	Sock *n;
	int i;
	*err = 0;
	if (s->type != SOCK_STREAM || s->state != SS_LISTEN) {
		*err = -EINVAL;
		return NULL;
	}
	while (s->nacc == 0) {
		*err = sock_wait(s, nonblock);
		if (*err) return NULL;
	}
	n = s->acceptq[0];
	for (i = 1; i < s->nacc; i++) s->acceptq[i - 1] = s->acceptq[i];
	s->nacc--;
	n->parent = NULL;
	n->pid = curproc->pid;
	return n;
}

int sys_accept(int fd, unsigned uaddr) {
	int err = 0, r;
	Sock *s = fd_sock(fd, &err), *n;
	if (!s) return err;
	n = sock_accept(s, s->nonblock, &err);
	if (!n) return err;
	r = sock_fd(n);
	if (r < 0) {
		if (n->domain == AF_INET) tcp_close(n);
		else sock_free(n);
		return r;
	}
	if (n->domain == AF_INET) write_addr(uaddr, AF_INET, n->faddr, n->fport, NULL);
	else write_addr(uaddr, AF_UNIX, 0, 0, n->peer && n->peer->path[0] ? n->peer->path : "");
	return r;
}

static int unix_send(Sock *s, const char *buf, unsigned n, int nonblock, const char *to) {
	unsigned done = 0;
	int r;
	if (s->shut_wr) {
		send_sigpipe();
		return -EPIPE;
	}
	if (s->type == SOCK_DGRAM) {
		int err = 0;
		Sock *d = unix_lookup(to ? to : s->peer_path, SOCK_DGRAM, &err);
		Dgram *g;
		if (!to && !s->peer_path[0]) return -ENOTCONN;
		if (!d) return err;
		if (n > DGRAM_LIMIT) return -EMSGSIZE;
		while (d->dq_bytes + n > DGRAM_LIMIT) {
			r = sock_wait(d, nonblock);
			if (r) return r;
		}
		g = kmalloc(sizeof(Dgram) + n);
		if (!g) return -ENOBUFS;
		memset(g, 0, sizeof(Dgram));
		g->len = n;
		strncpy(g->path, s->path, 107);
		memcpy(g->data, buf, n);
		if (d->dqt) d->dqt->next = g;
		else d->dq = g;
		d->dqt = g;
		d->dq_bytes += n;
		sock_wake(d);
		return (int)n;
	}
	if (s->state != SS_CONNECTED) return -ENOTCONN;
	while (done < n) {
		Sock *p = s->peer;
		unsigned k;
		if (!p || p->shut_rd) {
			if (done) return (int)done;
			send_sigpipe();
			return -EPIPE;
		}
		k = ring_put(&p->rx, buf + done, n - done);
		if (k) {
			done += k;
			sock_wake(p);
			continue;
		}
		if (done && nonblock) return (int)done;
		r = sock_wait(s, nonblock);
		if (r) return done ? (int)done : r;
	}
	return (int)done;
}

static int dgram_recv(Sock *s, char *buf, unsigned n, int flags, int nonblock, unsigned uaddr) {
	Dgram *g;
	int r;
	while (!s->dq) {
		if (s->error) {
			r = -s->error;
			s->error = 0;
			return r;
		}
		if (s->shut_rd) return 0;
		r = sock_wait(s, nonblock || (flags & MSG_DONTWAIT));
		if (r) return r;
	}
	g = s->dq;
	if (n > g->len) n = g->len;
	memcpy(buf, g->data, n);
	if (s->domain == AF_INET) write_addr(uaddr, AF_INET, g->saddr, g->sport, NULL);
	else write_addr(uaddr, AF_UNIX, 0, 0, g->path);
	if (!(flags & MSG_PEEK)) {
		s->dq = g->next;
		if (!s->dq) s->dqt = NULL;
		s->dq_bytes -= g->len;
		kfree(g);
		sock_wake(s);
	}
	return (int)n;
}

static int unix_recv(Sock *s, char *buf, unsigned n, int flags, int nonblock) {
	int r;
	unsigned k;
	if (s->state != SS_CONNECTED && !s->rx.len) return s->state == SS_CLOSED ? 0 : -ENOTCONN;
	while (!s->rx.len) {
		if (!s->peer || s->peer->shut_wr || s->shut_rd) return 0;
		r = sock_wait(s, nonblock || (flags & MSG_DONTWAIT));
		if (r) return r;
	}
	k = ring_get(&s->rx, buf, n, flags & MSG_PEEK);
	if (s->peer) sock_wake(s->peer);
	return (int)k;
}

static int do_send(Sock *s, const char *buf, unsigned n, int flags, int to_family, unsigned addr, unsigned port, const char *path) {
	int nb = s->nonblock || (flags & MSG_DONTWAIT), r;
	if (s->domain == AF_UNIX) return unix_send(s, buf, n, nb, to_family ? path : NULL);
	if (s->type == SOCK_STREAM) {
		r = tcp_send(s, buf, n, nb);
		net_run();
		return r;
	}
	if (!to_family) {
		if (s->state != SS_CONNECTED) return -EDESTADDRREQ;
		addr = s->faddr;
		port = s->fport;
	}
	r = s->type == SOCK_RAW ? raw_send(s, addr, buf, n) : udp_send(s, addr, port, buf, n);
	net_run();
	return r;
}

static int do_recv(Sock *s, char *buf, unsigned n, int flags, unsigned uaddr) {
	int nb = s->nonblock, r;
	if (s->type != SOCK_STREAM) return dgram_recv(s, buf, n, flags, nb, uaddr);
	if (s->domain == AF_UNIX) return unix_recv(s, buf, n, flags, nb);
	r = tcp_recv(s, buf, n, flags, nb);
	if (r > 0 && uaddr) write_addr(uaddr, AF_INET, s->faddr, s->fport, NULL);
	net_run();
	return r;
}

int sys_sendto(int fd, unsigned ubuf, unsigned n, int flags, unsigned uaddr) {
	int err = 0, family = 0, r;
	unsigned addr = 0, port = 0;
	char path[108], *k;
	Sock *s = fd_sock(fd, &err);
	if (!s) return err;
	if (uaddr) {
		r = read_addr(uaddr, &family, &addr, &port, path);
		if (r) return r;
		if (family != s->domain) return -EAFNOSUPPORT;
		if (s->type == SOCK_STREAM) family = 0;
	}
	if (n > 65536) n = 65536;
	k = kmalloc(n ? n : 1);
	if (!k) return -ENOMEM;
	if (copyin(k, ubuf, n)) {
		kfree(k);
		return -EFAULT;
	}
	r = do_send(s, k, n, flags, family, addr, port, path);
	kfree(k);
	return r;
}

int sys_recvfrom(int fd, unsigned ubuf, unsigned n, int flags, unsigned uaddr) {
	int err = 0, r;
	char *k;
	Sock *s = fd_sock(fd, &err);
	if (!s) return err;
	if (n > 65536) n = 65536;
	k = kmalloc(n ? n : 1);
	if (!k) return -ENOMEM;
	r = do_recv(s, k, n, flags, uaddr);
	if (r > 0 && copyout(ubuf, k, (unsigned)r)) r = -EFAULT;
	kfree(k);
	return r;
}

int sock_read(File *f, char *buf, unsigned n) {
	Sock *s = f->sock;
	s->nonblock = f->nonblock;
	return do_recv(s, buf, n, 0, 0);
}

int sock_write(File *f, const char *buf, unsigned n) {
	Sock *s = f->sock;
	s->nonblock = f->nonblock;
	return do_send(s, buf, n, 0, 0, 0, 0, NULL);
}

int sys_sockshutdown(int fd, int how) {
	int err = 0;
	Sock *s = fd_sock(fd, &err);
	if (!s) return err;
	return sock_shutdown(s, how);
}

int sock_shutdown(Sock *s, int how) {
	if (how < 0 || how > 2) return -EINVAL;
	if (s->type == SOCK_STREAM && s->state != SS_CONNECTED && !(s->tcb)) return -ENOTCONN;
	if (how != 1) s->shut_rd = 1;
	if (how != 0) s->shut_wr = 1;
	if (s->domain == AF_INET && s->type == SOCK_STREAM) {
		tcp_shutdown(s, how);
		net_run();
	}
	if (s->peer) sock_wake(s->peer);
	sock_wake(s);
	return 0;
}

int sys_setsockopt(int fd, int level, int name, unsigned uval, unsigned len) {
	int err = 0, v = 0;
	Sock *s = fd_sock(fd, &err);
	if (!s) return err;
	if (len < 4 || copyin(&v, uval, 4)) return -EFAULT;
	if (level == SOL_SOCKET && name == SO_REUSEADDR) {
		s->reuse = v != 0;
		return 0;
	}
	if (level == SOL_SOCKET && name == SO_RCVTIMEO) {
		s->rcvtimeo = v < 0 ? 0 : (unsigned)v;
		return 0;
	}
	if (level == IPPROTO_TCP && name == TCP_NODELAY) return 0;
	return -ENOPROTOOPT;
}

int sys_getsockname(int fd, unsigned uaddr) {
	int err = 0;
	Sock *s = fd_sock(fd, &err);
	if (!s) return err;
	if (s->domain == AF_UNIX) return write_addr(uaddr, AF_UNIX, 0, 0, s->path);
	return write_addr(uaddr, AF_INET, s->laddr, s->lport, NULL);
}

int sys_getpeername(int fd, unsigned uaddr) {
	int err = 0;
	Sock *s = fd_sock(fd, &err);
	if (!s) return err;
	if (s->state != SS_CONNECTED) return -ENOTCONN;
	if (s->domain == AF_UNIX) return write_addr(uaddr, AF_UNIX, 0, 0, s->type == SOCK_STREAM && s->peer ? s->peer->path : s->peer_path);
	return write_addr(uaddr, AF_INET, s->faddr, s->fport, NULL);
}

int sys_socketpair(int domain, int type, int proto, unsigned ufds) {
	Sock *a, *b;
	int fds[2];
	(void)proto;
	if (domain != AF_UNIX) return -EOPNOTSUPP;
	if (type != SOCK_STREAM && type != SOCK_DGRAM) return -EINVAL;
	a = sock_alloc(AF_UNIX, type, 0);
	b = sock_alloc(AF_UNIX, type, 0);
	if (!a || !b) {
		if (a) sock_free(a);
		if (b) sock_free(b);
		return -ENFILE;
	}
	if (ring_init(&a->rx) || ring_init(&b->rx)) {
		sock_free(a);
		sock_free(b);
		return -ENOBUFS;
	}
	a->peer = b;
	b->peer = a;
	a->state = b->state = SS_CONNECTED;
	if (type == SOCK_DGRAM) a->type = b->type = SOCK_STREAM;
	fds[0] = sock_fd(a);
	if (fds[0] < 0) {
		sock_free(a);
		sock_free(b);
		return fds[0];
	}
	fds[1] = sock_fd(b);
	if (fds[1] < 0) {
		sys_close(fds[0]);
		sock_free(b);
		return fds[1];
	}
	if (copyout(ufds, fds, 8)) return -EFAULT;
	return 0;
}

int sock_poll(File *f) {
	Sock *s = f->sock;
	int m = 0;
	if (s->domain == AF_INET && s->type == SOCK_STREAM) return tcp_poll(s);
	if (s->state == SS_LISTEN) return s->nacc ? POLLIN : 0;
	if (s->type != SOCK_STREAM) {
		if (s->dq || s->error) m |= POLLIN;
		if (s->error) m |= POLLERR;
		return m | POLLOUT;
	}
	if (s->rx.len) m |= POLLIN;
	if (s->state == SS_CONNECTED && (!s->peer || s->peer->shut_wr)) m |= POLLIN | POLLHUP;
	if (s->peer && ring_space(&s->peer->rx)) m |= POLLOUT;
	if (!s->peer && s->state == SS_CONNECTED) m |= POLLOUT;
	return m;
}

int sock_ioctl(File *f, unsigned req, unsigned arg) {
	return inet_ioctl(f->sock, req, arg);
}

void sock_close(File *f) {
	Sock *s = f->sock;
	int i;
	if (!s || --s->files > 0) return;
	if (s->domain == AF_INET && s->type == SOCK_STREAM) {
		tcp_close(s);
		net_run();
		return;
	}
	if (s->state == SS_LISTEN)
		for (i = 0; i < s->nacc; i++) {
			Sock *n = s->acceptq[i];
			if (n->peer) {
				n->peer->peer = NULL;
				sock_wake(n->peer);
			}
			sock_free(n);
		}
	if (s->peer) {
		s->peer->peer = NULL;
		s->peer->state = SS_CONNECTED;
		sock_wake(s->peer);
	}
	sock_free(s);
}
