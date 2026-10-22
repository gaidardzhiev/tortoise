/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include "kernel.h"
#include "net.h"

#define NET_BASE 0xF0008000u
#define N_CTRL (NET_BASE + 0x08)
#define N_STATUS (NET_BASE + 0x0C)
#define N_TXADDR (NET_BASE + 0x10)
#define N_TXLEN (NET_BASE + 0x14)
#define N_RXADDR (NET_BASE + 0x18)
#define N_RXLEN (NET_BASE + 0x1C)
#define N_DROP (NET_BASE + 0x28)

#define NARP 16
#define ARP_PENDING 4
#define ARP_TTL 6000
#define ARP_RETRY 100
#define NLOQ 64
#define NTXQ 32

#define FIONBIO 0x5421

typedef struct Pkt {
	struct Pkt *next;
	Netif *nif;
	unsigned len;
	char data[4];
} Pkt;

typedef struct Arp {
	int used, resolved, tries;
	unsigned ip;
	unsigned char mac[6];
	int expire, retry_at;
	Pkt *pending;
	int npending;
} Arp;

static int lo_xmit(Netif *nif, unsigned nexthop, char *pkt, unsigned len);
static int eth_xmit(Netif *nif, unsigned nexthop, char *pkt, unsigned len);

Netif lo_if = {"lo", 0x7F000001, 0xFF000000, IFF_UP | IFF_LOOPBACK | IFF_RUNNING, {0, 0, 0, 0, 0, 0}, 4136, 0, 0, 0, lo_xmit};
Netif eth_if = {"eth0", 0, 0, 0, {0, 0, 0, 0, 0, 0}, 1500, 0, 0, 0, eth_xmit};
Route routes[NROUTE];
static Arp arps[NARP];
static Pkt *loq, *loqt;
static int nloq;
static Pkt *txq, *txqt;
static int ntxq, eth_busy, eth_present;
static char *eth_rxbuf, *eth_txbuf;
static unsigned short ip_id = 1;
static int in_net_run;

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

static Pkt *pkt_new(Netif *nif, const char *d, unsigned len) {
	Pkt *p = kmalloc(sizeof(Pkt) + len);
	if (!p) return NULL;
	p->next = NULL;
	p->nif = nif;
	p->len = len;
	memcpy(p->data, d, len);
	return p;
}

static void route_auto(void) {
	int i, k = -1;
	for (i = 0; i < NROUTE; i++)
		if (routes[i].used && routes[i].auto_added && routes[i].nif == &eth_if) routes[i].used = 0;
	if (!(eth_if.flags & IFF_UP) || !eth_if.addr) return;
	for (i = 0; i < NROUTE; i++)
		if (!routes[i].used) {
			k = i;
			break;
		}
	if (k < 0) return;
	routes[k].used = 1;
	routes[k].dst = eth_if.addr & eth_if.mask;
	routes[k].mask = eth_if.mask;
	routes[k].gw = 0;
	routes[k].nif = &eth_if;
	routes[k].auto_added = 1;
}

static Route *route_lookup(unsigned dst) {
	int i, best = -1;
	unsigned bm = 0;
	for (i = 0; i < NROUTE; i++) {
		Route *r = &routes[i];
		if (!r->used || (dst & r->mask) != r->dst || !(r->nif->flags & IFF_UP)) continue;
		if (best < 0 || r->mask > bm || (r->mask == bm && r->auto_added)) {
			best = i;
			bm = r->mask;
		}
	}
	return best < 0 ? NULL : &routes[best];
}

int is_local_addr(unsigned a) {
	if ((a & 0xFF000000) == 0x7F000000) return 1;
	return a && a == eth_if.addr && (eth_if.flags & IFF_UP);
}

unsigned ip_source_for(unsigned dst) {
	Route *r;
	if (dst == 0xFFFFFFFF) return (eth_if.flags & IFF_UP) ? eth_if.addr : 0;
	if (is_local_addr(dst)) return (dst & 0xFF000000) == 0x7F000000 ? 0x7F000001 : dst;
	r = route_lookup(dst);
	return r ? r->nif->addr : 0;
}

static int lo_xmit(Netif *nif, unsigned nexthop, char *pkt, unsigned len) {
	Pkt *p;
	(void)nexthop;
	if (nloq >= NLOQ) {
		nif->drops++;
		return -ENOBUFS;
	}
	p = pkt_new(nif, pkt, len);
	if (!p) return -ENOBUFS;
	if (loqt) loqt->next = p;
	else loq = p;
	loqt = p;
	nloq++;
	nif->tx++;
	return 0;
}

static void eth_start(void) {
	Pkt *p;
	if (eth_busy || !txq || !eth_present) return;
	p = txq;
	txq = p->next;
	if (!txq) txqt = NULL;
	ntxq--;
	memcpy(eth_txbuf, p->data, p->len);
	eth_busy = 1;
	wr(N_TXADDR, (unsigned)eth_txbuf);
	wr(N_TXLEN, p->len < 60 ? 60 : p->len);
	kfree(p);
}

static int eth_frame(const unsigned char *dmac, unsigned type, const char *payload, unsigned len) {
	char f[1514];
	Pkt *p;
	if (!eth_present || len > 1500) return -EMSGSIZE;
	if (ntxq >= NTXQ) {
		eth_if.drops++;
		return -ENOBUFS;
	}
	memcpy(f, dmac, 6);
	memcpy(f + 6, eth_if.mac, 6);
	put16(f + 12, type);
	memcpy(f + 14, payload, len);
	if (len + 14 < 60) memset(f + 14 + len, 0, 60 - 14 - len);
	p = pkt_new(&eth_if, f, len + 14 < 60 ? 60 : len + 14);
	if (!p) return -ENOBUFS;
	if (txqt) txqt->next = p;
	else txq = p;
	txqt = p;
	ntxq++;
	eth_if.tx++;
	eth_start();
	return 0;
}

static void arp_send(unsigned op, const unsigned char *tmac, unsigned tip, const unsigned char *dmac) {
	char a[28];
	put16(a, 1);
	put16(a + 2, 0x0800);
	a[4] = 6;
	a[5] = 4;
	put16(a + 6, op);
	memcpy(a + 8, eth_if.mac, 6);
	put32(a + 14, eth_if.addr);
	memcpy(a + 18, tmac, 6);
	put32(a + 24, tip);
	eth_frame(dmac, 0x0806, a, 28);
}

static int arp_nwait;

static int arp_waiting(void) {
	return arp_nwait;
}

static void arp_drop_pending(Arp *a, int count) {
	if (a->npending) arp_nwait--;
	while (a->pending) {
		Pkt *n = a->pending->next;
		kfree(a->pending);
		a->pending = n;
		if (count) eth_if.drops++;
	}
	a->npending = 0;
}

static Arp *arp_find(unsigned ip) {
	int i;
	for (i = 0; i < NARP; i++)
		if (arps[i].used && arps[i].ip == ip) return &arps[i];
	return NULL;
}

static Arp *arp_slot(unsigned ip) {
	int i, old = 0;
	Arp *a = arp_find(ip);
	if (a) return a;
	for (i = 0; i < NARP; i++) {
		if (!arps[i].used) {
			old = i;
			break;
		}
		if (arps[i].expire < arps[old].expire) old = i;
	}
	a = &arps[old];
	arp_drop_pending(a, 0);
	memset(a, 0, sizeof(Arp));
	a->used = 1;
	a->ip = ip;
	return a;
}

static void arp_learn(unsigned ip, const unsigned char *mac, int create) {
	Arp *a = arp_find(ip);
	static const unsigned char zero[6] = {0, 0, 0, 0, 0, 0};
	if (!a && !create) return;
	if (!a) a = arp_slot(ip);
	memcpy(a->mac, mac, 6);
	a->resolved = 1;
	a->expire = (int)ticks + ARP_TTL;
	if (a->npending) arp_nwait--;
	while (a->pending) {
		Pkt *p = a->pending;
		a->pending = p->next;
		eth_frame(a->mac, 0x0800, p->data, p->len);
		kfree(p);
	}
	a->npending = 0;
	(void)zero;
}

static int eth_xmit(Netif *nif, unsigned nexthop, char *pkt, unsigned len) {
	static const unsigned char bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
	static const unsigned char zero[6] = {0, 0, 0, 0, 0, 0};
	Arp *a;
	Pkt *p;
	if (!(nif->flags & IFF_UP)) return -ENETDOWN;
	if (nexthop == 0xFFFFFFFF || nexthop == (nif->addr | ~nif->mask)) return eth_frame(bcast, 0x0800, pkt, len);
	a = arp_find(nexthop);
	if (a && a->resolved && a->expire > (int)ticks) return eth_frame(a->mac, 0x0800, pkt, len);
	if (!a) {
		a = arp_slot(nexthop);
		a->tries = 1;
		a->retry_at = (int)ticks + ARP_RETRY;
		a->expire = (int)ticks + ARP_TTL;
		arp_send(1, zero, nexthop, bcast);
	} else if (a->resolved) {
		a->resolved = 0;
		a->tries = 1;
		a->retry_at = (int)ticks + ARP_RETRY;
		arp_send(1, zero, nexthop, bcast);
	}
	if (a->npending >= ARP_PENDING) {
		nif->drops++;
		return 0;
	}
	p = pkt_new(nif, pkt, len);
	if (!p) return -ENOBUFS;
	if (a->npending == 0) arp_nwait++;
	if (!a->pending) a->pending = p;
	else {
		Pkt *q = a->pending;
		while (q->next) q = q->next;
		q->next = p;
	}
	a->npending++;
	return 0;
}

static void arp_input(const char *a, unsigned len) {
	unsigned op, sip, tip;
	if (len < 28 || get16(a) != 1 || get16(a + 2) != 0x0800) return;
	if (!memcmp(a + 8, eth_if.mac, 6)) return;
	op = get16(a + 6);
	sip = get32(a + 14);
	tip = get32(a + 24);
	if (!eth_if.addr) return;
	arp_learn(sip, (const unsigned char *)a + 8, tip == eth_if.addr);
	if (op == 1 && tip == eth_if.addr) arp_send(2, (const unsigned char *)a + 8, sip, (const unsigned char *)a + 8);
}

int ip_output(unsigned src, unsigned dst, int proto, char *payload, unsigned len) {
	char *pkt;
	Route *r;
	Netif *nif;
	unsigned nexthop;
	int rc;
	if (len + 20 > 4200) return -EMSGSIZE;
	if (is_local_addr(dst)) {
		nif = &lo_if;
		nexthop = dst;
		if (!src) src = (dst & 0xFF000000) == 0x7F000000 ? 0x7F000001 : dst;
	} else {
		r = route_lookup(dst);
		if (!r && dst == 0xFFFFFFFF && (eth_if.flags & IFF_UP)) {
			nif = &eth_if;
			nexthop = dst;
		} else {
			if (!r) return -ENETUNREACH;
			nif = r->nif;
			nexthop = r->gw ? r->gw : dst;
		}
		if (!src) src = nif->addr;
	}
	if (len + 20 > nif->mtu) return -EMSGSIZE;
	pkt = kmalloc(len + 20);
	if (!pkt) return -ENOBUFS;
	pkt[0] = 0x45;
	pkt[1] = 0;
	put16(pkt + 2, len + 20);
	put16(pkt + 4, ip_id++);
	put16(pkt + 6, 0x4000);
	pkt[8] = 64;
	pkt[9] = (char)proto;
	put16(pkt + 10, 0);
	put32(pkt + 12, src);
	put32(pkt + 16, dst);
	put16(pkt + 10, csum(pkt, 20, 0));
	memcpy(pkt + 20, payload, len);
	rc = nif->xmit(nif, nexthop, pkt, len + 20);
	kfree(pkt);
	return rc;
}

void ip_input(Netif *nif, char *pkt, unsigned len) {
	unsigned hl, tl, src, dst, flags;
	nif->rx++;
	if (len < 20 || (pkt[0] & 0xF0) != 0x40) return;
	hl = (unsigned)(pkt[0] & 15) * 4;
	tl = get16(pkt + 2);
	if (hl < 20 || tl < hl || tl > len || csum(pkt, hl, 0) != 0) return;
	flags = get16(pkt + 6);
	if ((flags & 0x2000) || (flags & 0x1FFF)) return;
	src = get32(pkt + 12);
	dst = get32(pkt + 16);
	if (!is_local_addr(dst) && dst != 0xFFFFFFFF && !(nif == &eth_if && eth_if.addr && dst == (eth_if.addr | ~eth_if.mask))) return;
	switch ((unsigned char)pkt[9]) {
	case IPPROTO_ICMP:
		icmp_input(src, dst, pkt + hl, tl - hl, pkt, tl);
		break;
	case IPPROTO_TCP:
		tcp_input(src, dst, pkt + hl, tl - hl);
		break;
	case IPPROTO_UDP:
		udp_input(src, dst, pkt + hl, tl - hl);
		break;
	}
}

int net_run(void) {
	int n = 0;
	if (in_net_run) return 0;
	in_net_run = 1;
	while (loq) {
		n++;
		Pkt *p = loq;
		loq = p->next;
		if (!loq) loqt = NULL;
		nloq--;
		ip_input(&lo_if, p->data, p->len);
		kfree(p);
	}
	in_net_run = 0;
	return n;
}

void eth_intr(void) {
	unsigned st = rd(N_STATUS);
	if (!(st & 2) && eth_busy) {
		eth_busy = 0;
		eth_start();
	}
	while (st & 1) {
		unsigned len = rd(N_RXLEN);
		char *f = eth_rxbuf;
		if (len >= 14) ether_deliver(f, len);
		if (len >= 14) {
			unsigned type = get16(f + 12);
			if (type == 0x0806) arp_input(f + 14, len - 14);
			else if (type == 0x0800 && (eth_if.flags & IFF_UP)) {
				char *copy = kmalloc(len);
				if (copy) {
					memcpy(copy, f + 14, len - 14);
					wr(N_STATUS, 1);
					ip_input(&eth_if, copy, len - 14);
					kfree(copy);
					st = rd(N_STATUS);
					continue;
				}
			}
		}
		wr(N_STATUS, 1);
		st = rd(N_STATUS);
	}
	net_run();
}

int ether_send(const unsigned char *dmac, unsigned type, const char *payload, unsigned len) {
	int r = eth_frame(dmac, type, payload, len);
	net_run();
	return r;
}

int ether_present(void) {
	return eth_present;
}

void net_init(void) {
	unsigned lo, hi;
	memset(routes, 0, sizeof(routes));
	routes[0].used = 1;
	routes[0].dst = 0x7F000000;
	routes[0].mask = 0xFF000000;
	routes[0].nif = &lo_if;
	lo = rd(NET_BASE);
	hi = rd(NET_BASE + 4);
	if (lo || hi) {
		eth_present = 1;
		eth_if.mac[0] = (unsigned char)lo;
		eth_if.mac[1] = (unsigned char)(lo >> 8);
		eth_if.mac[2] = (unsigned char)(lo >> 16);
		eth_if.mac[3] = (unsigned char)(lo >> 24);
		eth_if.mac[4] = (unsigned char)hi;
		eth_if.mac[5] = (unsigned char)(hi >> 8);
		random_mix(lo ^ (hi << 16));
		eth_rxbuf = (char *)low_alloc();
		eth_txbuf = (char *)low_alloc();
		wr(N_RXADDR, (unsigned)eth_rxbuf);
		wr(N_CTRL, 7);
		eth_if.flags = (rd(N_STATUS) & 4) ? IFF_RUNNING : 0;
	}
}

void net_tick(void) {
	int i;
	extern int tcp_ntcb;
	if (!tcp_ntcb && !arp_waiting() && !loq) return;
	for (i = 0; i < NARP; i++) {
		Arp *a = &arps[i];
		if (!a->used) continue;
		if (!a->resolved && a->pending && (int)ticks >= a->retry_at) {
			static const unsigned char bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
			static const unsigned char zero[6] = {0, 0, 0, 0, 0, 0};
			if (a->tries >= 3) {
				arp_drop_pending(a, 1);
				a->used = 0;
				continue;
			}
			a->tries++;
			a->retry_at = (int)ticks + ARP_RETRY;
			arp_send(1, zero, a->ip, bcast);
		} else if (a->resolved && (int)ticks >= a->expire) {
			a->used = 0;
		}
	}
	if (tcp_ntcb) tcp_tick();
	net_run();
}

int inet_port_used(int type, unsigned addr, unsigned port, Sock *except) {
	int i;
	for (i = 0; i < NSOCK; i++) {
		Sock *s = &socks[i];
		if (!s->used || s == except || s->domain != AF_INET || s->type != type || s->lport != port) continue;
		if (s->tcb && s->tcb->orphan) continue;
		if (!addr || !s->laddr || s->laddr == addr) return 1;
	}
	if (type == SOCK_STREAM)
		for (i = 0;; i++) {
			Tcb *t = tcp_tcbs(i);
			if (!t) break;
			if (t->used && t->orphan && t->lport == port && (!addr || !t->laddr || t->laddr == addr)) return 2;
		}
	return 0;
}

unsigned inet_ephemeral(int type) {
	static unsigned next = 49152;
	unsigned i;
	for (i = 0; i < 16384; i++) {
		unsigned p = next++;
		if (next > 65535) next = 49152;
		if (!inet_port_used(type, 0, p, NULL)) return p;
	}
	return 0;
}

int inet_bind(Sock *s, unsigned addr, unsigned port) {
	int u;
	if (s->lport) return -EINVAL;
	if (addr && !is_local_addr(addr)) return -EADDRNOTAVAIL;
	if (s->type == SOCK_RAW) {
		s->laddr = addr;
		return 0;
	}
	if (port && port < 1024 && curproc && curproc->euid) return -EACCES;
	if (!port) {
		port = inet_ephemeral(s->type);
		if (!port) return -EADDRINUSE;
	} else {
		u = inet_port_used(s->type, addr, port, s);
		if (u == 1 || (u == 2 && !s->reuse)) return -EADDRINUSE;
	}
	s->laddr = addr;
	s->lport = (unsigned short)port;
	return 0;
}

static Sock *udp_find(unsigned dst, unsigned dport, unsigned src, unsigned sport) {
	int i;
	Sock *best = NULL;
	for (i = 0; i < NSOCK; i++) {
		Sock *s = &socks[i];
		if (!s->used || s->domain != AF_INET || s->type != SOCK_DGRAM || s->lport != dport) continue;
		if (s->laddr && s->laddr != dst && dst != 0xFFFFFFFF) continue;
		if (s->state == SS_CONNECTED && (s->faddr != src || s->fport != sport)) continue;
		if (!best || s->state == SS_CONNECTED) best = s;
	}
	return best;
}

static void dgram_queue(Sock *s, unsigned src, unsigned sport, const char *d, unsigned n) {
	Dgram *g;
	if (s->dq_bytes + n > DGRAM_LIMIT) return;
	g = kmalloc(sizeof(Dgram) + n);
	if (!g) return;
	memset(g, 0, sizeof(Dgram));
	g->len = n;
	g->saddr = src;
	g->sport = (unsigned short)sport;
	memcpy(g->data, d, n);
	if (s->dqt) s->dqt->next = g;
	else s->dq = g;
	s->dqt = g;
	s->dq_bytes += n;
	sock_wake(s);
}

static unsigned pseudo(unsigned src, unsigned dst, int proto, unsigned len) {
	char ph[12];
	put32(ph, src);
	put32(ph + 4, dst);
	ph[8] = 0;
	ph[9] = (char)proto;
	put16(ph + 10, len);
	return csum_add(ph, 12, 0);
}

void udp_input(unsigned src, unsigned dst, char *p, unsigned len) {
	unsigned sport, dport, ulen;
	Sock *s;
	if (len < 8) return;
	sport = get16(p);
	dport = get16(p + 2);
	ulen = get16(p + 4);
	if (ulen < 8 || ulen > len) return;
	if (get16(p + 6) && csum_fold(csum_add(p, ulen, pseudo(src, dst, IPPROTO_UDP, ulen))) != 0) return;
	s = udp_find(dst, dport, src, sport);
	if (!s) {
		if (dst != 0xFFFFFFFF) {
			char ip[28 + 8];
			char hdr[20];
			hdr[0] = 0x45;
			hdr[1] = 0;
			put16(hdr + 2, ulen + 20);
			put16(hdr + 4, 0);
			put16(hdr + 6, 0x4000);
			hdr[8] = 64;
			hdr[9] = IPPROTO_UDP;
			put16(hdr + 10, 0);
			put32(hdr + 12, src);
			put32(hdr + 16, dst);
			memcpy(ip, hdr, 20);
			memcpy(ip + 20, p, 8);
			icmp_unreachable(ip, 28, 3);
		}
		return;
	}
	dgram_queue(s, src, sport, p + 8, ulen - 8);
}

int udp_send(Sock *s, unsigned dst, unsigned dport, const char *buf, unsigned n) {
	char *p;
	unsigned src;
	unsigned short c;
	int r;
	if (!dport) return -EINVAL;
	if (n > 4096 - 28) return -EMSGSIZE;
	if (!s->lport) {
		r = inet_bind(s, 0, 0);
		if (r) return r;
	}
	src = s->laddr ? s->laddr : ip_source_for(dst);
	if (!src && dst != 0xFFFFFFFF) return -ENETUNREACH;
	p = kmalloc(n + 8);
	if (!p) return -ENOBUFS;
	put16(p, s->lport);
	put16(p + 2, dport);
	put16(p + 4, n + 8);
	put16(p + 6, 0);
	memcpy(p + 8, buf, n);
	c = csum_fold(csum_add(p, n + 8, pseudo(src, dst, IPPROTO_UDP, n + 8)));
	put16(p + 6, c ? c : 0xFFFF);
	r = ip_output(src, dst, IPPROTO_UDP, p, n + 8);
	kfree(p);
	return r < 0 ? r : (int)n;
}

int raw_send(Sock *s, unsigned dst, const char *buf, unsigned n) {
	int r = ip_output(s->laddr, dst, IPPROTO_ICMP, (char *)buf, n);
	return r < 0 ? r : (int)n;
}

void icmp_unreachable(char *ip, unsigned iplen, int code) {
	char m[8 + 28];
	unsigned src;
	if (iplen < 28) return;
	m[0] = 3;
	m[1] = (char)code;
	put16(m + 2, 0);
	put32(m + 4, 0);
	memcpy(m + 8, ip, 28);
	put16(m + 2, csum(m, 36, 0));
	src = get32(ip + 16);
	ip_output(is_local_addr(src) ? src : 0, get32(ip + 12), IPPROTO_ICMP, m, 36);
}

void icmp_input(unsigned src, unsigned dst, char *p, unsigned len, char *ip, unsigned iplen) {
	int i;
	if (len < 8 || csum(p, len, 0) != 0) return;
	for (i = 0; i < NSOCK; i++) {
		Sock *s = &socks[i];
		if (s->used && s->domain == AF_INET && s->type == SOCK_RAW) dgram_queue(s, src, 0, ip, iplen);
	}
	if (p[0] == 8 && p[1] == 0) {
		char *r = kmalloc(len);
		if (!r) return;
		memcpy(r, p, len);
		r[0] = 0;
		put16(r + 2, 0);
		put16(r + 2, csum(r, len, 0));
		ip_output(dst == 0xFFFFFFFF ? 0 : dst, src, IPPROTO_ICMP, r, len);
		kfree(r);
	} else if (p[0] == 3 && len >= 8 + 28) {
		char *in = p + 8;
		unsigned ihl = (unsigned)(in[0] & 15) * 4;
		int err = p[1] == 3 ? ECONNREFUSED : EHOSTUNREACH;
		unsigned isrc, idst, sport, dport;
		if (len < 8 + ihl + 8) return;
		isrc = get32(in + 12);
		idst = get32(in + 16);
		sport = get16(in + ihl);
		dport = get16(in + ihl + 2);
		if (in[9] == IPPROTO_UDP) {
			for (i = 0; i < NSOCK; i++) {
				Sock *s = &socks[i];
				if (!s->used || s->domain != AF_INET || s->type != SOCK_DGRAM || s->lport != sport) continue;
				if (s->state == SS_CONNECTED && (s->faddr != idst || s->fport != dport)) continue;
				s->error = err;
				sock_wake(s);
			}
		} else if (in[9] == IPPROTO_TCP) {
			tcp_icmp_error(idst, (unsigned short)sport, (unsigned short)dport, err);
		}
		(void)isrc;
	}
}

static Netif *nif_named(const char *n) {
	if (!strcmp(n, "lo")) return &lo_if;
	if (!strcmp(n, "eth0")) return &eth_if;
	return NULL;
}

int route_set(int del, unsigned dst, unsigned mask, unsigned gw, const char *ifname) {
	Netif *nif;
	int i;
	if (del) {
		for (i = 0; i < NROUTE; i++)
			if (routes[i].used && routes[i].dst == dst && routes[i].mask == mask) {
				if (curproc->euid) return -EPERM;
				routes[i].used = 0;
				return 0;
			}
		return -ESRCH;
	}
	if (curproc->euid) return -EPERM;
	nif = ifname ? nif_named(ifname) : NULL;
	if (!nif) {
		Route *r = route_lookup(gw);
		if (gw && r) nif = r->nif;
	}
	if (!nif) return gw ? -ENETUNREACH : -ENODEV;
	for (i = 0; i < NROUTE; i++)
		if (!routes[i].used) {
			routes[i].used = 1;
			routes[i].dst = dst & mask;
			routes[i].mask = mask;
			routes[i].gw = gw;
			routes[i].nif = nif;
			routes[i].auto_added = 0;
			return 0;
		}
	return -ENOBUFS;
}

int netif_set(Netif *nif, unsigned req, unsigned v) {
	if (curproc->euid) return -EPERM;
	if (nif == &lo_if) return req == SIOCSIFFLAGS ? 0 : -EPERM;
	if (req == SIOCSIFFLAGS) {
		if (!eth_present) return -ENODEV;
		nif->flags = (nif->flags & ~(IFF_UP | IFF_PROMISC)) | (v & (IFF_UP | IFF_PROMISC));
		wr(N_CTRL, (nif->flags & IFF_PROMISC) ? 15 : 7);
	} else if (req == SIOCSIFADDR) {
		nif->addr = v;
		if (!nif->mask) nif->mask = (v >> 24) < 128 ? 0xFF000000 : (v >> 24) < 192 ? 0xFFFF0000 : 0xFFFFFF00;
	} else {
		nif->mask = v;
	}
	route_auto();
	if (nif == &eth_if && (eth_if.flags & IFF_UP) && eth_if.addr) {
		static const unsigned char bc[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
		static const unsigned char z[6] = {0, 0, 0, 0, 0, 0};
		arp_send(1, z, eth_if.addr, bc);
	}
	return 0;
}

int inet_ioctl(Sock *s, unsigned req, unsigned arg) {
	char ifr[32];
	Netif *nif;
	unsigned v;
	(void)s;
	if (req == FIONBIO) return 0;
	if (req == SIOCGIFCONF) {
		int conf[2], n = 0;
		Netif *list[2];
		int i;
		if (copyin(conf, arg, 8)) return -EFAULT;
		list[0] = &lo_if;
		list[1] = &eth_if;
		for (i = 0; i < 2 && (n + 1) * 32 <= conf[0]; i++) {
			memset(ifr, 0, sizeof(ifr));
			strncpy(ifr, list[i]->name, 15);
			put32(ifr + 16, list[i]->addr);
			if (copyout((unsigned)conf[1] + (unsigned)n * 32, ifr, 32)) return -EFAULT;
			n++;
		}
		conf[0] = n * 32;
		return copyout(arg, conf, 8) ? -EFAULT : 0;
	}
	if (req == SIOCADDRT || req == SIOCDELRT) {
		char rt[28];
		unsigned dst, mask, gw;
		int i;
		if (copyin(rt, arg, 28)) return -EFAULT;
		dst = get32(rt);
		mask = get32(rt + 4);
		gw = get32(rt + 8);
		rt[27] = 0;
		return route_set(req == SIOCDELRT, dst, mask, gw, rt[12] ? rt + 12 : NULL);
	}
	if (req != SIOCGIFFLAGS && req != SIOCSIFFLAGS && req != SIOCGIFADDR && req != SIOCSIFADDR && req != SIOCGIFNETMASK && req != SIOCSIFNETMASK && req != SIOCGIFHWADDR) return -ENOTTY;
	if (copyin(ifr, arg, 32)) return -EFAULT;
	ifr[15] = 0;
	nif = nif_named(ifr);
	if (!nif) return -ENODEV;
	v = get32(ifr + 16);
	switch (req) {
	case SIOCGIFFLAGS:
		memset(ifr + 16, 0, 16);
		ifr[16] = (char)nif->flags;
		ifr[17] = (char)(nif->flags >> 8);
		break;
	case SIOCGIFADDR:
		memset(ifr + 16, 0, 16);
		put32(ifr + 16, nif->addr);
		break;
	case SIOCGIFNETMASK:
		memset(ifr + 16, 0, 16);
		put32(ifr + 16, nif->mask);
		break;
	case SIOCGIFHWADDR:
		memset(ifr + 16, 0, 16);
		memcpy(ifr + 16, nif->mac, 6);
		break;
	default:
		if (req == SIOCSIFFLAGS) v = (unsigned)(unsigned char)ifr[16] | ((unsigned)(unsigned char)ifr[17] << 8);
		return netif_set(nif, req, v);
	}
	return copyout(arg, ifr, 32) ? -EFAULT : 0;
}

static unsigned addstr(char *b, unsigned at, unsigned max, const char *s) {
	unsigned l = strlen(s);
	if (at + l >= max) l = max - at - 1;
	memcpy(b + at, s, l);
	return at + l;
}

static void dotted(char *out, unsigned a) {
	ksnprintf(out, 16, "%u.%u.%u.%u", a >> 24, (a >> 16) & 255, (a >> 8) & 255, a & 255);
}

int net_proc(int which, char *b, unsigned max) {
	char line[160], a1[16], a2[16], a3[16];
	unsigned at = 0;
	int i;
	if (which == 0) {
		Netif *l[2];
		l[0] = &lo_if;
		l[1] = &eth_if;
		for (i = 0; i < 2; i++) {
			ksnprintf(line, sizeof(line), "%s %u %u %u\n", l[i]->name, l[i]->rx, l[i]->tx, l[i]->drops + (i == 1 && eth_present ? rd(N_DROP) : 0));
			at = addstr(b, at, max, line);
		}
	} else if (which == 1 || which == 2) {
		for (i = 0; i < NSOCK; i++) {
			Sock *s = &socks[i];
			if (!s->used || s->domain != AF_INET || s->type != (which == 1 ? SOCK_STREAM : SOCK_DGRAM)) continue;
			if (which == 1 && s->tcb) continue;
			dotted(a1, s->laddr);
			dotted(a2, s->faddr);
			ksnprintf(line, sizeof(line), "%s:%u %s:%u %s %u %u %d\n", a1, s->lport, a2, s->fport, which == 1 ? (s->state == SS_LISTEN ? "LISTEN" : "CLOSED") : (s->state == SS_CONNECTED ? "CONNECTED" : "UNCONNECTED"), which == 2 ? s->dq_bytes : 0, 0, s->pid);
			at = addstr(b, at, max, line);
		}
		if (which == 1)
			for (i = 0;; i++) {
				Tcb *t = tcp_tcbs(i);
				if (!t) break;
				if (!t->used) continue;
				dotted(a1, t->laddr);
				dotted(a2, t->faddr);
				ksnprintf(line, sizeof(line), "%s:%u %s:%u %s %u %u %d\n", a1, t->lport, a2, t->fport, tcp_state_name(t->state), t->s ? t->s->rx.len : 0, t->s ? t->s->tx.len : 0, t->s && !t->orphan ? t->s->pid : 0);
				at = addstr(b, at, max, line);
			}
	} else if (which == 3) {
		for (i = 0; i < NSOCK; i++) {
			Sock *s = &socks[i];
			if (!s->used || s->domain != AF_UNIX) continue;
			ksnprintf(line, sizeof(line), "%s %s %s\n", s->path[0] ? s->path : "-", s->type == SOCK_STREAM ? "stream" : "dgram", s->state == SS_LISTEN ? "LISTEN" : s->state == SS_CONNECTED ? "CONNECTED" : "UNCONNECTED");
			at = addstr(b, at, max, line);
		}
	} else if (which == 4) {
		for (i = 0; i < NARP; i++) {
			Arp *a = &arps[i];
			if (!a->used || !a->resolved) continue;
			dotted(a1, a->ip);
			int k;
			char mac[18];
			for (k = 0; k < 6; k++) {
				mac[k * 3] = "0123456789abcdef"[a->mac[k] >> 4];
				mac[k * 3 + 1] = "0123456789abcdef"[a->mac[k] & 15];
				mac[k * 3 + 2] = k < 5 ? ':' : 0;
			}
			ksnprintf(line, sizeof(line), "%s %s eth0\n", a1, mac);
			at = addstr(b, at, max, line);
		}
	} else if (which == 5) {
		for (i = 0; i < NROUTE; i++) {
			Route *r = &routes[i];
			if (!r->used) continue;
			dotted(a1, r->dst);
			dotted(a2, r->mask);
			dotted(a3, r->gw);
			ksnprintf(line, sizeof(line), "%s %s %s %s\n", a1, a2, a3, r->nif->name);
			at = addstr(b, at, max, line);
		}
	}
	b[at] = 0;
	return (int)at;
}
