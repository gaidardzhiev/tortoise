/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#ifndef _SYS_SOCKET_H
#define _SYS_SOCKET_H
#include <stddef.h>
#define AF_UNSPEC 0
#define AF_UNIX 1
#define AF_LOCAL 1
#define AF_INET 2
#define PF_UNIX AF_UNIX
#define PF_INET AF_INET
#define SOCK_STREAM 1
#define SOCK_DGRAM 2
#define SOCK_RAW 3
#define MSG_PEEK 2
#define MSG_DONTWAIT 0x40
#define SOL_SOCKET 1
#define SO_REUSEADDR 2
#define SO_RCVTIMEO 20
#define SHUT_RD 0
#define SHUT_WR 1
#define SHUT_RDWR 2
#define SOMAXCONN 16
typedef unsigned socklen_t;
typedef unsigned short sa_family_t;
struct sockaddr {
	sa_family_t sa_family;
	char sa_data[14];
};
int socket(int domain, int type, int proto);
int bind(int fd, const struct sockaddr *addr, socklen_t len);
int listen(int fd, int backlog);
int accept(int fd, struct sockaddr *addr, socklen_t *len);
int connect(int fd, const struct sockaddr *addr, socklen_t len);
int send(int fd, const void *buf, size_t n, int flags);
int recv(int fd, void *buf, size_t n, int flags);
int sendto(int fd, const void *buf, size_t n, int flags, const struct sockaddr *addr, socklen_t len);
int recvfrom(int fd, void *buf, size_t n, int flags, struct sockaddr *addr, socklen_t *len);
int shutdown(int fd, int how);
int setsockopt(int fd, int level, int name, const void *val, socklen_t len);
int getsockname(int fd, struct sockaddr *addr, socklen_t *len);
int getpeername(int fd, struct sockaddr *addr, socklen_t *len);
int socketpair(int domain, int type, int proto, int fds[2]);
#endif
