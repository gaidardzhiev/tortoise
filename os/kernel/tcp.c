/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include "kernel.h"
#include "net.h"

#define NTCB 32
#define F_FIN 1
#define F_SYN 2
#define F_RST 4
#define F_PSH 8
#define F_ACK 16
#define RTO_INIT 100
#define MAX_RETRIES 6
#define TIME_WAIT_TICKS 200

static Tcb tcbs[NTCB];
int tcp_ntcb;

#define SEQ_LT(a, b) ((int)((a) - (b)) < 0)
#define SEQ_LE(a, b) ((int)((a) - (b)) <= 0)

static const char *names[] = {"CLOSED", "LISTEN", "SYN_SENT", "SYN_RECEIVED", "ESTABLISHED", "FIN_WAIT_1", "FIN_WAIT_2", "CLOSE_WAIT", "CLOSING", "LAST_ACK", "TIME_WAIT"};

const char *tcp_state_name(int st) {
	return st >= 0 && st <= TCP_TIME_WAIT ? names[st] : "?";
}

Tcb *tcp_tcbs(int i) {
	return i < NTCB ? &tcbs[i] : NULL;
}

static unsigned get16(const char *p) {
	return (unsigned)((unsigned char)p[0] << 8 | (unsigned char)p[1]);
}

static unsigned get32(const char *p) {
	return (unsigned)(unsigned char)p[0] << 24 | (unsigned)(unsigned char)p[1] << 16 | (unsigned)(unsigned char)p[2] << 8 | (unsigned char)p[3];
}

static void put16(char *p, unsigned v) {
	p[0] = (char)(v >> 8);
	p[1] = (char)v;
}

static void put32(char *p, unsigned v) {
	p[0] = (char)(v >> 24);
	p[1] = (char)(v >> 16);
	p[2] = (char)(v >> 8);
	p[3] = (char)v;
}

static unsigned our_mss(unsigned addr) {
	return is_local_addr(addr) ? 4096 : 1460;
}

static Tcb *tcb_alloc(Sock *s) {
	int i;
	for (i = 0; i < NTCB; i++)
		if (!tcbs[i].used) {
			Tcb *t = &tcbs[i];
			memset(t, 0, sizeof(Tcb));
			t->used = 1;
			tcp_ntcb++;
			t->s = s;
			t->rto = RTO_INIT;
			t->mss = 536;
			s->tcb = t;
			return t;
		}
	return NULL;
}

static void tcb_free(Tcb *t) {
	Sock *s = t->s;
	if (t->used) tcp_ntcb--;
	t->used = 0;
	if (!s) return;
	s->tcb = NULL;
	if (t->orphan) sock_free(s);
	else {
		s->state = SS_CLOSED;
		sock_wake(s);
	}
}

static void send_seg(unsigned src, unsigned dst, unsigned sport, unsigned dport, unsigned seq, unsigned ack, int flags, unsigned win, const char *data, unsigned len, unsigned mss) {
	char *p;
	unsigned hl = mss ? 24 : 20, sum;
	char ph[12];
	p = kmalloc(hl + len);
	if (!p) return;
	put16(p, sport);
	put16(p + 2, dport);
	put32(p + 4, seq);
	put32(p + 8, ack);
	p[12] = (char)((hl / 4) << 4);
	p[13] = (char)flags;
	put16(p + 14, win > 65535 ? 65535 : win);
	put16(p + 16, 0);
	put16(p + 18, 0);
	if (mss) {
		p[20] = 2;
		p[21] = 4;
		put16(p + 22, mss);
	}
	if (len) memcpy(p + hl, data, len);
	put32(ph, src);
	put32(ph + 4, dst);
	ph[8] = 0;
	ph[9] = IPPROTO_TCP;
	put16(ph + 10, hl + len);
	sum = csum_add(ph, 12, 0);
	put16(p + 16, csum_fold(csum_add(p, hl + len, sum)));
	ip_output(src, dst, IPPROTO_TCP, p, hl + len);
	kfree(p);
}

static unsigned rwin(Tcb *t) {
	return t->s ? ring_space(&t->s->rx) : 0;
}

static void tcb_seg(Tcb *t, unsigned seq, int flags, const char *data, unsigned len, int with_mss) {
	send_seg(t->laddr, t->faddr, t->lport, t->fport, seq, t->rcv_nxt, flags, rwin(t), data, len, with_mss ? our_mss(t->faddr) : 0);
}

static void arm(Tcb *t) {
	if (!t->rtx_at) t->rtx_at = (int)ticks + t->rto;
}

static void tcp_output(Tcb *t) {
	Sock *s = t->s;
	int sent = 0;
	char *buf;
	if (t->state == TCP_SYN_SENT || t->state == TCP_SYN_RECEIVED || t->state == TCP_CLOSED || t->state == TCP_LISTEN) {
		if (t->ack_now && t->state == TCP_SYN_RECEIVED) tcb_seg(t, t->iss, F_SYN | F_ACK, NULL, 0, 1);
		t->ack_now = 0;
		return;
	}
	buf = kmalloc(4096);
	if (!buf) return;
	for (;;) {
		unsigned off = t->snd_nxt - t->snd_una, avail, win, n;
		if (t->fin_sent) break;
		avail = s->tx.len > off ? s->tx.len - off : 0;
		win = t->snd_una + t->snd_wnd - t->snd_nxt;
		if ((int)win < 0) win = 0;
		n = avail;
		if (n > win) n = win;
		if (n > t->mss) n = t->mss;
		if (n > 4096) n = 4096;
		if (n == 0) break;
		ring_peek_at(&s->tx, off, buf, n);
		tcb_seg(t, t->snd_nxt, F_ACK | F_PSH, buf, n, 0);
		t->snd_nxt += n;
		if (SEQ_LT(t->snd_max, t->snd_nxt)) t->snd_max = t->snd_nxt;
		sent = 1;
		arm(t);
	}
	if (!t->fin_sent && s->shut_wr && t->snd_nxt - t->snd_una == s->tx.len) {
		tcb_seg(t, t->snd_nxt, F_FIN | F_ACK, NULL, 0, 0);
		t->snd_nxt++;
		if (SEQ_LT(t->snd_max, t->snd_nxt)) t->snd_max = t->snd_nxt;
		t->fin_sent = 1;
		sent = 1;
		arm(t);
		if (t->state == TCP_ESTABLISHED) t->state = TCP_FIN_WAIT_1;
		else if (t->state == TCP_CLOSE_WAIT) t->state = TCP_LAST_ACK;
	}
	kfree(buf);
	if (s->tx.len > t->snd_nxt - t->snd_una && t->snd_wnd == 0) arm(t);
	if (t->ack_now && !sent) tcb_seg(t, t->snd_nxt, F_ACK, NULL, 0, 0);
	t->ack_now = 0;
}

static void tcp_fail(Tcb *t, int err) {
	if (t->s) {
		t->s->error = err;
		sock_wake(t->s);
	}
	tcb_free(t);
}

static void send_reset(unsigned src, unsigned dst, const char *p, unsigned dlen) {
	unsigned flags = (unsigned char)p[13], seq = get32(p + 4), ack = get32(p + 8);
	if (flags & F_RST) return;
	if (flags & F_ACK) send_seg(dst, src, get16(p + 2), get16(p), ack, 0, F_RST, 0, NULL, 0, 0);
	else send_seg(dst, src, get16(p + 2), get16(p), 0, seq + dlen + ((flags & F_SYN) ? 1 : 0) + ((flags & F_FIN) ? 1 : 0), F_RST | F_ACK, 0, NULL, 0, 0);
}

static Sock *find_listener(unsigned dst, unsigned port) {
	int i;
	Sock *best = NULL;
	for (i = 0; i < NSOCK; i++) {
		Sock *s = &socks[i];
		if (!s->used || s->domain != AF_INET || s->type != SOCK_STREAM || s->state != SS_LISTEN || s->lport != port) continue;
		if (s->laddr && s->laddr != dst) continue;
		if (!best || s->laddr) best = s;
	}
	return best;
}

static int pending_children(Sock *l) {
	int i, n = 0;
	for (i = 0; i < NTCB; i++)
		if (tcbs[i].used && tcbs[i].state == TCP_SYN_RECEIVED && tcbs[i].s && tcbs[i].s->parent == l) n++;
	return n;
}

static void parse_mss(Tcb *t, const char *p, unsigned hl) {
	unsigned i = 20;
	while (i < hl) {
		unsigned char k = (unsigned char)p[i];
		if (k == 0) break;
		if (k == 1) {
			i++;
			continue;
		}
		if (i + 1 >= hl) break;
		if (k == 2 && p[i + 1] == 4 && i + 4 <= hl) {
			unsigned m = get16(p + i + 2);
			if (m >= 64) t->mss = m;
		}
		if ((unsigned char)p[i + 1] < 2) break;
		i += (unsigned char)p[i + 1];
	}
	if (t->mss > our_mss(t->faddr)) t->mss = our_mss(t->faddr);
}

static void established(Tcb *t) {
	Sock *s = t->s, *l = s->parent;
	t->state = TCP_ESTABLISHED;
	s->state = SS_CONNECTED;
	if (l) {
		if (l->used && l->state == SS_LISTEN && l->nacc < ACCEPTMAX) {
			l->acceptq[l->nacc++] = s;
			sock_wake(l);
		} else {
			tcb_seg(t, t->snd_nxt, F_RST, NULL, 0, 0);
			t->orphan = 1;
			tcb_free(t);
			return;
		}
	}
	sock_wake(s);
}

void tcp_input(unsigned src, unsigned dst, char *p, unsigned len) {
	unsigned hl, sport, dport, seq, ack, win, flags, dlen, i;
	char ph[12];
	Tcb *t = NULL;
	char *data;
	if (len < 20) return;
	put32(ph, src);
	put32(ph + 4, dst);
	ph[8] = 0;
	ph[9] = IPPROTO_TCP;
	put16(ph + 10, len);
	if (csum_fold(csum_add(p, len, csum_add(ph, 12, 0))) != 0) return;
	hl = ((unsigned char)p[12] >> 4) * 4;
	if (hl < 20 || hl > len) return;
	sport = get16(p);
	dport = get16(p + 2);
	seq = get32(p + 4);
	ack = get32(p + 8);
	flags = (unsigned char)p[13];
	win = get16(p + 14);
	data = p + hl;
	dlen = len - hl;
	for (i = 0; i < NTCB; i++)
		if (tcbs[i].used && tcbs[i].lport == dport && tcbs[i].fport == sport && tcbs[i].faddr == src && tcbs[i].laddr == dst) {
			t = &tcbs[i];
			break;
		}
	if (!t) {
		Sock *l = find_listener(dst, dport), *c;
		if (!l || !(flags & F_SYN) || (flags & (F_ACK | F_RST))) {
			if (!(flags & F_RST) && !(l && !(flags & F_SYN) && !(flags & F_ACK))) send_reset(src, dst, p, dlen);
			return;
		}
		if (l->nacc + pending_children(l) >= l->backlog) return;
		c = sock_alloc(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (!c) return;
		if (ring_init(&c->rx) || ring_init(&c->tx)) {
			sock_free(c);
			return;
		}
		t = tcb_alloc(c);
		if (!t) {
			sock_free(c);
			return;
		}
		c->parent = l;
		c->laddr = dst;
		c->lport = (unsigned short)dport;
		c->faddr = src;
		c->fport = (unsigned short)sport;
		c->state = SS_CONNECTING;
		t->laddr = dst;
		t->lport = (unsigned short)dport;
		t->faddr = src;
		t->fport = (unsigned short)sport;
		t->state = TCP_SYN_RECEIVED;
		t->irs = seq;
		t->rcv_nxt = seq + 1;
		t->iss = kernel_random();
		t->snd_una = t->iss;
		t->snd_nxt = t->snd_max = t->iss + 1;
		t->snd_wnd = win;
		parse_mss(t, p, hl);
		tcb_seg(t, t->iss, F_SYN | F_ACK, NULL, 0, 1);
		arm(t);
		return;
	}
	if (t->state == TCP_SYN_SENT) {
		if ((flags & F_ACK) && (SEQ_LE(ack, t->iss) || SEQ_LT(t->snd_nxt, ack))) {
			if (!(flags & F_RST)) send_reset(src, dst, p, dlen);
			return;
		}
		if (flags & F_RST) {
			if (flags & F_ACK) tcp_fail(t, ECONNREFUSED);
			return;
		}
		if (!(flags & F_SYN)) return;
		t->irs = seq;
		t->rcv_nxt = seq + 1;
		t->snd_wnd = win;
		parse_mss(t, p, hl);
		if (flags & F_ACK) {
			t->snd_una = ack;
			t->rtx_at = 0;
			t->retries = 0;
			t->rto = RTO_INIT;
			established(t);
			t->ack_now = 1;
			tcp_output(t);
		} else {
			t->state = TCP_SYN_RECEIVED;
			tcb_seg(t, t->iss, F_SYN | F_ACK, NULL, 0, 1);
		}
		return;
	}
	{
		unsigned seglen = dlen + ((flags & F_FIN) ? 1 : 0);
		if (SEQ_LT(seq, t->rcv_nxt)) {
			unsigned skip = t->rcv_nxt - seq;
			if (flags & F_SYN) {
				skip = skip > 0 ? skip - 1 : 0;
				flags &= ~F_SYN;
				seq++;
			}
			if (skip >= seglen && !(seglen == 0 && skip == 0)) {
				if (!(flags & F_RST)) {
					t->ack_now = 1;
					tcp_output(t);
				}
				return;
			}
			if (skip > dlen) skip = dlen;
			data += skip;
			dlen -= skip;
			seq += skip;
		} else if (seq != t->rcv_nxt) {
			if (!(flags & F_RST)) {
				t->ack_now = 1;
				tcp_output(t);
			}
			return;
		}
	}
	if (flags & F_RST) {
		if (t->state == TCP_SYN_RECEIVED && t->s && t->s->parent) {
			t->orphan = 1;
			tcb_free(t);
		} else if (t->state == TCP_TIME_WAIT || t->state == TCP_LAST_ACK || t->state == TCP_CLOSING) {
			tcb_free(t);
		} else {
			tcp_fail(t, ECONNRESET);
		}
		return;
	}
	if (flags & F_SYN) {
		tcb_seg(t, t->snd_nxt, F_RST, NULL, 0, 0);
		tcp_fail(t, ECONNRESET);
		return;
	}
	if (!(flags & F_ACK)) return;
	if (t->state == TCP_SYN_RECEIVED) {
		if (SEQ_LT(t->snd_una, ack) && SEQ_LE(ack, t->snd_nxt)) {
			t->snd_una = ack;
			t->snd_wnd = win;
			t->rtx_at = 0;
			t->retries = 0;
			t->rto = RTO_INIT;
			established(t);
			if (!t->used) return;
		} else {
			send_reset(src, dst, p, dlen);
			return;
		}
	}
	if (SEQ_LT(t->snd_una, ack) && SEQ_LE(ack, t->snd_max)) {
		unsigned acked = ack - t->snd_una, dacked = acked;
		Sock *s = t->s;
		if (t->fin_sent && ack == t->snd_max) {
			dacked--;
			t->fin_acked = 1;
		}
		if (dacked > s->tx.len) dacked = s->tx.len;
		ring_drop(&s->tx, dacked);
		t->snd_una = ack;
		if (SEQ_LT(t->snd_nxt, ack)) t->snd_nxt = ack;
		t->retries = 0;
		t->rto = RTO_INIT;
		t->rtx_at = t->snd_una == t->snd_max ? 0 : (int)ticks + t->rto;
		sock_wake(s);
	} else if (SEQ_LT(t->snd_max, ack)) {
		t->ack_now = 1;
		tcp_output(t);
		return;
	}
	if (SEQ_LE(t->snd_una, ack)) t->snd_wnd = win;
	if (t->fin_acked) {
		if (t->state == TCP_FIN_WAIT_1) t->state = TCP_FIN_WAIT_2;
		else if (t->state == TCP_CLOSING) {
			t->state = TCP_TIME_WAIT;
			t->tw_at = (int)ticks + TIME_WAIT_TICKS;
		} else if (t->state == TCP_LAST_ACK) {
			tcb_free(t);
			return;
		}
	}
	if (dlen && t->orphan) {
		send_seg(t->laddr, t->faddr, t->lport, t->fport, ack, 0, F_RST, 0, NULL, 0, 0);
		tcb_free(t);
		return;
	}
	if (dlen && (t->state == TCP_ESTABLISHED || t->state == TCP_FIN_WAIT_1 || t->state == TCP_FIN_WAIT_2)) {
		Sock *s = t->s;
		unsigned k = s->shut_rd ? dlen : ring_put(&s->rx, data, dlen);
		t->rcv_nxt += k;
		t->ack_now = 1;
		if (k < dlen) flags &= ~F_FIN;
		sock_wake(s);
	} else if (dlen) {
		t->ack_now = 1;
		flags &= ~F_FIN;
	}
	if (flags & F_FIN) {
		t->rcv_nxt++;
		t->fin_rcvd = 1;
		t->ack_now = 1;
		if (t->state == TCP_ESTABLISHED) t->state = TCP_CLOSE_WAIT;
		else if (t->state == TCP_FIN_WAIT_1) {
			if (t->fin_acked) {
				t->state = TCP_TIME_WAIT;
				t->tw_at = (int)ticks + TIME_WAIT_TICKS;
			} else {
				t->state = TCP_CLOSING;
			}
		} else if (t->state == TCP_FIN_WAIT_2) {
			t->state = TCP_TIME_WAIT;
			t->tw_at = (int)ticks + TIME_WAIT_TICKS;
		} else if (t->state == TCP_TIME_WAIT) {
			t->tw_at = (int)ticks + TIME_WAIT_TICKS;
		}
		if (t->s) sock_wake(t->s);
	}
	tcp_output(t);
}

int tcp_connect(Sock *s, unsigned addr, unsigned port) {
	Tcb *t;
	int r;
	if (s->state == SS_LISTEN) return -EINVAL;
	if (s->tcb) return s->tcb->state == TCP_SYN_SENT ? -EALREADY : -EISCONN;
	if (s->state == SS_CONNECTED || s->state == SS_CLOSED) return -EISCONN;
	if (!port) return -ECONNREFUSED;
	if (!s->lport) {
		r = inet_bind(s, 0, 0);
		if (r) return r;
	}
	if (!s->laddr && !ip_source_for(addr)) return -ENETUNREACH;
	if (ring_init(&s->rx) || ring_init(&s->tx)) return -ENOBUFS;
	t = tcb_alloc(s);
	if (!t) return -ENOBUFS;
	t->laddr = s->laddr ? s->laddr : ip_source_for(addr);
	t->lport = s->lport;
	t->faddr = addr;
	t->fport = (unsigned short)port;
	s->faddr = addr;
	s->fport = (unsigned short)port;
	if (!s->laddr) s->laddr = t->laddr;
	t->iss = kernel_random();
	t->snd_una = t->iss;
	t->snd_nxt = t->snd_max = t->iss + 1;
	t->state = TCP_SYN_SENT;
	s->state = SS_CONNECTING;
	s->error = 0;
	tcb_seg(t, t->iss, F_SYN, NULL, 0, 1);
	arm(t);
	if (s->nonblock) return -EINPROGRESS;
	while (s->tcb && s->tcb->state == TCP_SYN_SENT) {
		r = sock_wait(s, 0);
		if (r == -EINTR) return r;
	}
	if (s->state == SS_CONNECTED) return 0;
	r = s->error ? -s->error : -ECONNREFUSED;
	s->error = 0;
	return r;
}

int tcp_listen(Sock *s, int backlog) {
	int r;
	if (s->tcb || s->state == SS_CONNECTED) return -EINVAL;
	if (!s->lport) {
		r = inet_bind(s, 0, 0);
		if (r) return r;
	}
	s->state = SS_LISTEN;
	s->backlog = backlog;
	return 0;
}

int tcp_send(Sock *s, const char *buf, unsigned n, int nonblock) {
	unsigned done = 0;
	int r;
	while (s->tcb && s->tcb->state == TCP_SYN_SENT) {
		r = sock_wait(s, nonblock);
		if (r) return r;
	}
	for (;;) {
		Tcb *t = s->tcb;
		unsigned k;
		if (s->error) {
			r = -s->error;
			s->error = 0;
			if (r == -ECONNRESET || r == -ETIMEDOUT) {
				if (done) return (int)done;
				return r;
			}
		}
		if (!t || s->shut_wr || (t->state != TCP_ESTABLISHED && t->state != TCP_CLOSE_WAIT)) {
			if (done) return (int)done;
			if (s->state != SS_CONNECTED && s->state != SS_CLOSED && !t) return -ENOTCONN;
			send_sigpipe();
			return -EPIPE;
		}
		if (done == n) return (int)done;
		k = ring_put(&s->tx, buf + done, n - done);
		done += k;
		tcp_output(t);
		net_run();
		if (done == n) return (int)done;
		if (!k) {
			if (nonblock) return done ? (int)done : -EAGAIN;
			r = sock_wait(s, 0);
			if (r) return done ? (int)done : r;
		}
	}
}

void tcp_after_read(Sock *s) {
	Tcb *t = s->tcb;
	if (t && (t->state == TCP_ESTABLISHED || t->state == TCP_FIN_WAIT_1 || t->state == TCP_FIN_WAIT_2)) {
		t->ack_now = 1;
		tcp_output(t);
	}
}

int tcp_recv(Sock *s, char *buf, unsigned n, int flags, int nonblock) {
	int r;
	unsigned k, before;
	if (s->state == SS_LISTEN) return -ENOTCONN;
	for (;;) {
		if (s->rx.len) break;
		if (s->error) {
			r = -s->error;
			s->error = 0;
			return r;
		}
		if (s->shut_rd || !s->tcb || s->tcb->fin_rcvd) return 0;
		if (s->state != SS_CONNECTED && s->state != SS_CONNECTING) return -ENOTCONN;
		r = sock_wait(s, nonblock || (flags & MSG_DONTWAIT));
		if (r) return r;
	}
	before = ring_space(&s->rx);
	k = ring_get(&s->rx, buf, n, flags & MSG_PEEK);
	if (!(flags & MSG_PEEK) && before < 4096 && ring_space(&s->rx) >= 4096) tcp_after_read(s);
	return (int)k;
}

int tcp_poll(Sock *s) {
	int m = 0;
	Tcb *t = s->tcb;
	if (s->state == SS_LISTEN) return s->nacc ? POLLIN : 0;
	if (s->rx.len) m |= POLLIN;
	if (s->error) m |= POLLERR | POLLIN;
	if (!t || t->fin_rcvd) m |= POLLIN | POLLHUP;
	if (t && (t->state == TCP_ESTABLISHED || t->state == TCP_CLOSE_WAIT) && ring_space(&s->tx) && !s->shut_wr) m |= POLLOUT;
	if (!t && s->state != SS_CONNECTING) m |= POLLOUT;
	return m;
}

void tcp_shutdown(Sock *s, int how) {
	Tcb *t = s->tcb;
	(void)how;
	if (!t) return;
	if (t->state == TCP_SYN_SENT) {
		t->orphan = 0;
		tcb_free(t);
		return;
	}
	tcp_output(t);
}

void tcp_close(Sock *s) {
	Tcb *t = s->tcb;
	int i;
	if (s->state == SS_LISTEN || !t) {
		if (s->state == SS_LISTEN) {
			for (i = 0; i < s->nacc; i++) {
				Sock *c = s->acceptq[i];
				if (c->tcb) {
					tcb_seg(c->tcb, c->tcb->snd_nxt, F_RST | F_ACK, NULL, 0, 0);
					c->tcb->orphan = 1;
					tcb_free(c->tcb);
				} else {
					sock_free(c);
				}
			}
			for (i = 0; i < NTCB; i++)
				if (tcbs[i].used && tcbs[i].s && tcbs[i].s->parent == s) {
					tcbs[i].orphan = 1;
					tcb_free(&tcbs[i]);
				}
		}
		sock_free(s);
		return;
	}
	t->orphan = 1;
	s->parent = NULL;
	if (t->state == TCP_SYN_SENT || t->state == TCP_CLOSED) {
		tcb_free(t);
		return;
	}
	if (s->rx.len && t->state != TCP_TIME_WAIT) {
		tcb_seg(t, t->snd_nxt, F_RST | F_ACK, NULL, 0, 0);
		tcb_free(t);
		return;
	}
	s->shut_wr = 1;
	tcp_output(t);
}

void tcp_icmp_error(unsigned dst, unsigned short sport, unsigned short dport, int err) {
	int i;
	for (i = 0; i < NTCB; i++) {
		Tcb *t = &tcbs[i];
		if (t->used && t->faddr == dst && t->lport == sport && t->fport == dport && t->state == TCP_SYN_SENT) tcp_fail(t, err);
	}
}

void tcp_tick(void) {
	int i;
	for (i = 0; i < NTCB; i++) {
		Tcb *t = &tcbs[i];
		if (!t->used) continue;
		if (t->state == TCP_TIME_WAIT) {
			if ((int)ticks >= t->tw_at) tcb_free(t);
			continue;
		}
		if (!t->rtx_at || (int)ticks < t->rtx_at) continue;
		t->rtx_at = 0;
		if (t->snd_una == t->snd_max && t->snd_wnd == 0 && t->s && t->s->tx.len) {
			char c;
			ring_peek_at(&t->s->tx, 0, &c, 1);
			tcb_seg(t, t->snd_una, F_ACK, &c, 1, 0);
			t->rto = t->rto * 2 > 3200 ? 3200 : t->rto * 2;
			arm(t);
			continue;
		}
		if (t->snd_una == t->snd_max) continue;
		if (++t->retries > MAX_RETRIES) {
			tcb_seg(t, t->snd_nxt, F_RST | F_ACK, NULL, 0, 0);
			tcp_fail(t, ETIMEDOUT);
			continue;
		}
		t->rto *= 2;
		if (t->state == TCP_SYN_SENT) {
			tcb_seg(t, t->iss, F_SYN, NULL, 0, 1);
		} else if (t->state == TCP_SYN_RECEIVED) {
			tcb_seg(t, t->iss, F_SYN | F_ACK, NULL, 0, 1);
		} else {
			t->snd_nxt = t->snd_una;
			if (t->fin_sent && !t->fin_acked) t->fin_sent = 0;
			tcp_output(t);
		}
		arm(t);
	}
}
