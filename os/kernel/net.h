/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#define AF_UNIX 1
#define AF_INET 2
#define SOCK_STREAM 1
#define SOCK_DGRAM 2
#define SOCK_RAW 3
#define MSG_PEEK 2
#define MSG_DONTWAIT 0x40
#define SOL_SOCKET 1
#define SO_REUSEADDR 2
#define SO_RCVTIMEO 20
#define IPPROTO_ICMP 1
#define IPPROTO_TCP 6
#define IPPROTO_UDP 17
#define TCP_NODELAY 1

#define ENFILE 23
#define ENOTSOCK 88
#define EDESTADDRREQ 89
#define EMSGSIZE 90
#define EPROTOTYPE 91
#define ENOPROTOOPT 92
#define EPROTONOSUPPORT 93
#define EOPNOTSUPP 95
#define EAFNOSUPPORT 97
#define EADDRINUSE 98
#define EADDRNOTAVAIL 99
#define ENETDOWN 100
#define ENETUNREACH 101
#define ECONNABORTED 103
#define ECONNRESET 104
#define ENOBUFS 105
#define EISCONN 106
#define ENOTCONN 107
#define ETIMEDOUT 110
#define ECONNREFUSED 111
#define EHOSTUNREACH 113
#define EALREADY 114
#define EINPROGRESS 115

#define SS_FREE 0
#define SS_UNCONN 1
#define SS_LISTEN 2
#define SS_CONNECTING 3
#define SS_CONNECTED 4
#define SS_CLOSED 5

#define NSOCK 64
#define RINGSIZE 8192
#define DGRAM_LIMIT 16384
#define ACCEPTMAX 16

typedef struct Ring {
	char *buf;
	unsigned size, head, len;
} Ring;

typedef struct Dgram {
	struct Dgram *next;
	unsigned len;
	unsigned saddr;
	unsigned short sport;
	char path[108];
	char data[4];
} Dgram;

struct Tcb;

typedef struct Sock {
	int used, id, domain, type, proto, state, error;
	int nonblock, reuse, shut_rd, shut_wr;
	unsigned rcvtimeo;
	int files;
	Ring rx, tx;
	Dgram *dq, *dqt;
	unsigned dq_bytes;
	WaitQueue wq;
	unsigned laddr, faddr;
	unsigned short lport, fport;
	char path[108];
	char peer_path[108];
	Vnode *bound_vn;
	struct Sock *peer;
	struct Sock *acceptq[ACCEPTMAX];
	int nacc, backlog;
	struct Sock *parent;
	struct Tcb *tcb;
	int pid;
	int neterr;
} Sock;

#define IFF_UP 1
#define IFF_PROMISC 0x100
#define SIOCADDRT 0x890B
#define SIOCDELRT 0x890C
#define SIOCGIFCONF 0x8912
#define SIOCGIFFLAGS 0x8913
#define SIOCSIFFLAGS 0x8914
#define SIOCGIFADDR 0x8915
#define SIOCSIFADDR 0x8916
#define SIOCGIFNETMASK 0x891B
#define SIOCSIFNETMASK 0x891C
#define SIOCGIFHWADDR 0x8927
#define IFF_LOOPBACK 8
#define IFF_RUNNING 0x40

typedef struct Netif {
	char name[16];
	unsigned addr, mask, flags;
	unsigned char mac[6];
	unsigned mtu;
	unsigned rx, tx, drops;
	int (*xmit)(struct Netif *nif, unsigned nexthop, char *pkt, unsigned len);
} Netif;

#define NROUTE 8

typedef struct Route {
	int used;
	unsigned dst, mask, gw;
	Netif *nif;
	int auto_added;
} Route;

#define TCP_CLOSED 0
#define TCP_LISTEN 1
#define TCP_SYN_SENT 2
#define TCP_SYN_RECEIVED 3
#define TCP_ESTABLISHED 4
#define TCP_FIN_WAIT_1 5
#define TCP_FIN_WAIT_2 6
#define TCP_CLOSE_WAIT 7
#define TCP_CLOSING 8
#define TCP_LAST_ACK 9
#define TCP_TIME_WAIT 10

typedef struct Tcb {
	int used, state;
	Sock *s;
	unsigned laddr, faddr;
	unsigned short lport, fport;
	unsigned iss, snd_una, snd_nxt, snd_wnd, snd_max;
	unsigned irs, rcv_nxt;
	unsigned mss;
	int fin_sent, fin_acked, fin_rcvd;
	int rto, retries, rtx_at, tw_at, ack_now;
	int orphan;
	struct Tcb *listener;
} Tcb;

extern Netif lo_if, eth_if;
extern Route routes[NROUTE];
extern Sock socks[NSOCK];

unsigned short htons_k(unsigned short v);
unsigned htonl_k(unsigned v);
unsigned short csum(const void *p, unsigned n, unsigned start);
unsigned csum_add(const void *p, unsigned n, unsigned sum);
unsigned short csum_fold(unsigned sum);

int ring_init(Ring *r);
void ring_free(Ring *r);
unsigned ring_space(Ring *r);
unsigned ring_put(Ring *r, const char *p, unsigned n);
unsigned ring_get(Ring *r, char *p, unsigned n, int peek);
void ring_peek_at(Ring *r, unsigned off, char *p, unsigned n);
void ring_drop(Ring *r, unsigned n);

void sock_wake(Sock *s);
Sock *sock_alloc(int domain, int type, int proto);
void sock_free(Sock *s);
int sock_wait(Sock *s, int nonblock);

int ip_output(unsigned src, unsigned dst, int proto, char *payload, unsigned len);
void ip_input(Netif *nif, char *pkt, unsigned len);
unsigned ip_source_for(unsigned dst);
int is_local_addr(unsigned a);
int net_run(void);
void net_init(void);
void net_tick(void);
void eth_intr(void);
int route_set(int del, unsigned dst, unsigned mask, unsigned gw, const char *ifname);
int netif_set(Netif *nif, unsigned req, unsigned v);

int udp_bind_port(Sock *s, unsigned addr, unsigned port);
int inet_bind(Sock *s, unsigned addr, unsigned port);
int inet_port_used(int type, unsigned addr, unsigned port, Sock *except);
unsigned inet_ephemeral(int type);
void udp_input(unsigned src, unsigned dst, char *p, unsigned len);
int udp_send(Sock *s, unsigned dst, unsigned dport, const char *buf, unsigned n);
void icmp_input(unsigned src, unsigned dst, char *p, unsigned len, char *ip, unsigned iplen);
void icmp_unreachable(char *ip, unsigned iplen, int code);
int raw_send(Sock *s, unsigned dst, const char *buf, unsigned n);

void tcp_input(unsigned src, unsigned dst, char *p, unsigned len);
int tcp_connect(Sock *s, unsigned addr, unsigned port);
int tcp_listen(Sock *s, int backlog);
int tcp_send(Sock *s, const char *buf, unsigned n, int nonblock);
int tcp_recv(Sock *s, char *buf, unsigned n, int flags, int nonblock);
int tcp_poll(Sock *s);
void tcp_close(Sock *s);
void tcp_shutdown(Sock *s, int how);
void tcp_tick(void);
void tcp_after_read(Sock *s);
void tcp_icmp_error(unsigned dst, unsigned short sport, unsigned short dport, int err);
const char *tcp_state_name(int st);
Tcb *tcp_tcbs(int i);

int inet_ioctl(Sock *s, unsigned req, unsigned arg);
int net_proc(int which, char *b, unsigned max);
Sock *sock_new(int domain, int type, int proto, int *err);
int sock_listen(Sock *s, int backlog);
int sock_connect_inet(Sock *s, unsigned addr, unsigned port);
Sock *sock_accept(Sock *s, int nonblock, int *err);
int sock_shutdown(Sock *s, int how);
int ether_send(const unsigned char *dmac, unsigned type, const char *payload, unsigned len);
int ether_present(void);
void ether_deliver(const char *frame, unsigned len);
