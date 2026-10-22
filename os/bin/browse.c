/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <poll.h>
#include <time.h>
#include <dirent.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <net9.h>

#define MAXDOC (1024 * 1024)
#define MAXURL 1024
#define MAXLINKS 2000
#define MAXLINES 20000
#define MAXHIST 32
#define MAXREDIR 5
#define TIMEOUT 1000

typedef struct Url {
	char scheme[8];
	char host[256];
	int port;
	char path[MAXURL];
} Url;

typedef struct Page {
	char url[MAXURL];
	char title[128];
	char *text;
	short *attr;
	int textlen, textcap;
	int *lines;
	int nlines;
	char *links[MAXLINKS];
	int nlinks;
	char note[160];
} Page;

typedef struct Hist {
	char url[MAXURL];
	int top, cur;
} Hist;

static Page page;
static Hist hist[MAXHIST];
static int nhist;
static int width = 80, rows = 25;
static int dump;
static struct termios saved;
static int raw;
static char status[256];

static void restore_tty(void) {
	if (raw) {
		tcsetattr(0, TCSANOW, &saved);
		fputs("\033[0m\033[2J\033[H", stdout);
		fflush(stdout);
		raw = 0;
	}
}

static void on_signal(int sig) {
	(void)sig;
	restore_tty();
	_exit(1);
}

static void setstatus(const char *a, const char *b) {
	int i;
	snprintf(status, sizeof(status), "%s%s", a, b ? b : "");
	for (i = 0; status[i]; i++)
		if ((unsigned char)status[i] < 32 || (unsigned char)status[i] > 126) status[i] = '?';
}

static int safe_url_char(int c) {
	return c > 32 && c < 127 && c != '"' && c != '<' && c != '>' && c != '\\' && c != '^' && c != '`' && c != '{' && c != '|' && c != '}';
}

static void clean_url(const char *in, char *out, int max) {
	int n = 0;
	for (; *in && n < max - 4; in++) {
		unsigned char c = (unsigned char)*in;
		if (c == '#') break;
		if (c == ' ') {
			out[n++] = '%';
			out[n++] = '2';
			out[n++] = '0';
		} else if (safe_url_char(c)) out[n++] = (char)c;
		else if (c >= 128 || c < 32) continue;
		else {
			out[n++] = '%';
			out[n++] = "0123456789ABCDEF"[c >> 4];
			out[n++] = "0123456789ABCDEF"[c & 15];
		}
	}
	out[n] = 0;
}

static int parse_url(const char *s, Url *u) {
	const char *p, *h;
	int n;
	memset(u, 0, sizeof(*u));
	if (!strncasecmp(s, "http://", 7)) {
		strcpy(u->scheme, "http");
		p = s + 7;
	} else if (!strncasecmp(s, "file:", 5)) {
		strcpy(u->scheme, "file");
		p = s + 5;
		while (p[0] == '/' && p[1] == '/') p++;
		if (*p != '/') return -1;
		strncpy(u->path, p, MAXURL - 1);
		return 0;
	} else return -1;
	h = p;
	for (n = 0; *p && *p != '/' && *p != ':' && *p != '?'; p++, n++) {
		if (!isalnum((unsigned char)*p) && *p != '.' && *p != '-') return -1;
		if (n >= (int)sizeof(u->host) - 1) return -1;
	}
	if (n == 0) return -1;
	memcpy(u->host, h, (unsigned)n);
	u->host[n] = 0;
	u->port = 80;
	if (*p == ':') {
		u->port = 0;
		for (p++; isdigit((unsigned char)*p); p++) {
			u->port = u->port * 10 + (*p - '0');
			if (u->port > 65535) return -1;
		}
		if (u->port == 0) return -1;
	}
	if (*p && *p != '/' && *p != '?') return -1;
	if (*p == '?') snprintf(u->path, MAXURL, "/%s", p);
	else strncpy(u->path, *p ? p : "/", MAXURL - 1);
	return 0;
}

static void url_string(Url *u, char *out) {
	if (!strcmp(u->scheme, "file")) snprintf(out, MAXURL, "file:%s", u->path);
	else if (u->port == 80) snprintf(out, MAXURL, "http://%s%s", u->host, u->path);
	else snprintf(out, MAXURL, "http://%s:%d%s", u->host, u->port, u->path);
}

static void normalize(char *path) {
	char out[MAXURL], *q = strchr(path, '?'), query[MAXURL];
	int n = 0;
	const char *p = path;
	query[0] = 0;
	if (q) {
		strncpy(query, q, MAXURL - 1);
		query[MAXURL - 1] = 0;
		*q = 0;
	}
	out[0] = 0;
	while (*p) {
		const char *seg;
		int len;
		while (*p == '/') p++;
		seg = p;
		while (*p && *p != '/') p++;
		len = (int)(p - seg);
		if (len == 0) break;
		if (len == 1 && seg[0] == '.') continue;
		if (len == 2 && seg[0] == '.' && seg[1] == '.') {
			while (n > 0 && out[n - 1] != '/') n--;
			if (n > 0) n--;
			out[n] = 0;
			continue;
		}
		if (n + len + 2 >= MAXURL) break;
		out[n++] = '/';
		memcpy(out + n, seg, (unsigned)len);
		n += len;
		out[n] = 0;
	}
	if (n == 0 || (p > path && p[-1] == '/' && n + 1 < MAXURL)) out[n++] = '/';
	out[n] = 0;
	if (n > 1 && out[n - 1] == '/' && out[n - 2] == '/') out[--n] = 0;
	snprintf(path, MAXURL, "%s%s", out, query);
}

static int resolve(const char *base, const char *ref, char *out) {
	Url b, r;
	char clean[MAXURL], tmp[MAXURL];
	clean_url(ref, clean, MAXURL);
	if (!clean[0]) {
		strncpy(out, base, MAXURL - 1);
		out[MAXURL - 1] = 0;
		return 0;
	}
	if (strchr(clean, ':') && strchr(clean, ':') < (strchr(clean, '/') ? strchr(clean, '/') : clean + strlen(clean))) {
		if (parse_url(clean, &r) < 0) {
			strncpy(out, clean, MAXURL - 1);
			out[MAXURL - 1] = 0;
			return -1;
		}
		if (!strcmp(r.scheme, "file") && strncmp(base, "file:", 5)) {
			strncpy(out, clean, MAXURL - 1);
			out[MAXURL - 1] = 0;
			return -2;
		}
		normalize(r.path);
		url_string(&r, out);
		return 0;
	}
	if (parse_url(base, &b) < 0) return -1;
	if (clean[0] == '/' && clean[1] == '/') {
		if (!strcmp(b.scheme, "file")) return -1;
		snprintf(tmp, MAXURL, "http:%s", clean);
		if (parse_url(tmp, &r) < 0) return -1;
		normalize(r.path);
		url_string(&r, out);
		return 0;
	}
	if (clean[0] == '/') strncpy(tmp, clean, MAXURL - 1);
	else if (clean[0] == '?') {
		char *q;
		strncpy(tmp, b.path, MAXURL - 1);
		q = strchr(tmp, '?');
		if (q) *q = 0;
		strncat(tmp, clean, MAXURL - strlen(tmp) - 1);
	} else {
		char *q, *slash;
		strncpy(tmp, b.path, MAXURL - 1);
		tmp[MAXURL - 1] = 0;
		q = strchr(tmp, '?');
		if (q) *q = 0;
		slash = strrchr(tmp, '/');
		if (slash) slash[1] = 0;
		strncat(tmp, clean, MAXURL - strlen(tmp) - 1);
	}
	tmp[MAXURL - 1] = 0;
	strcpy(b.path, tmp);
	normalize(b.path);
	url_string(&b, out);
	return 0;
}

static int read_timed(int fd, char *buf, int n) {
	struct pollfd pf;
	pf.fd = fd;
	pf.events = POLLIN;
	pf.revents = 0;
	if (poll(&pf, 1, TIMEOUT) <= 0) return -2;
	return read(fd, buf, (unsigned)n);
}

static int header_value(const char *hdr, const char *name, char *out, int max) {
	int l = (int)strlen(name);
	const char *p = hdr;
	while (*p) {
		const char *e = strchr(p, '\n');
		if (!strncasecmp(p, name, (unsigned)l) && p[l] == ':') {
			int n = 0;
			p += l + 1;
			while (*p == ' ' || *p == '\t') p++;
			while (*p && *p != '\r' && *p != '\n' && n < max - 1) out[n++] = *p++;
			out[n] = 0;
			return 0;
		}
		if (!e) break;
		p = e + 1;
	}
	out[0] = 0;
	return -1;
}

static char *read_file(const char *path, int *len, char *ctype) {
	struct stat st;
	char *doc;
	int fd, n = 0, r;
	if (stat(path, &st) < 0) return NULL;
	if (S_ISDIR(st.st_mode)) {
		DIR *d = opendir(path);
		struct dirent *e;
		doc = malloc(65536);
		if (!d || !doc) {
			if (d) closedir(d);
			free(doc);
			return NULL;
		}
		n = snprintf(doc, 65536, "<title>Directory %s</title><h1>Directory %s</h1><ul>", path, path);
		while ((e = readdir(d)) != NULL && n < 65000) {
			if (!strcmp(e->d_name, ".")) continue;
			if (strpbrk(e->d_name, "<>&\"'") || e->d_name[0] == 0) continue;
			n += snprintf(doc + n, (unsigned)(65536 - n), "<li><a href=\"%s%s\">%s</a>", e->d_name, strcmp(e->d_name, "..") ? "" : "/", e->d_name);
		}
		closedir(d);
		n += snprintf(doc + n, (unsigned)(65536 - n), "</ul>");
		strcpy(ctype, "text/html");
		*len = n;
		return doc;
	}
	fd = open(path, O_RDONLY);
	if (fd < 0) return NULL;
	doc = malloc(MAXDOC + 1);
	if (!doc) {
		close(fd);
		return NULL;
	}
	while (n < MAXDOC && (r = read(fd, doc + n, (unsigned)(MAXDOC - n))) > 0) n += r;
	close(fd);
	doc[n] = 0;
	*len = n;
	n = (int)strlen(path);
	if ((n > 5 && !strcasecmp(path + n - 5, ".html")) || (n > 4 && !strcasecmp(path + n - 4, ".htm"))) strcpy(ctype, "text/html");
	else strcpy(ctype, "text/plain");
	return doc;
}

static char *http_get(Url *u, int *len, char *ctype, char *location, int *code) {
	char addr[300], req[MAXURL + 400], *doc, hdr[8192];
	int fd, n = 0, r, hl = 0, total = 0, deadline;
	char *body;
	snprintf(addr, sizeof(addr), "tcp!%s!%d", u->host, u->port);
	fd = dial(addr, 0, 0, 0);
	if (fd < 0) {
		setstatus("cannot connect to ", u->host);
		return NULL;
	}
	n = snprintf(req, sizeof(req), "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: browse/1.0 (TORTOISE)\r\nAccept: text/html, text/plain\r\nConnection: close\r\n\r\n", u->path, u->host);
	if (write(fd, req, (unsigned)n) != n) {
		close(fd);
		setstatus("cannot send the request to ", u->host);
		return NULL;
	}
	doc = malloc(MAXDOC + sizeof(hdr) + 1);
	if (!doc) {
		close(fd);
		return NULL;
	}
	deadline = time() + 60;
	for (;;) {
		if (total >= MAXDOC + (int)sizeof(hdr)) break;
		if (time() > deadline) {
			setstatus("gave up after a minute reading from ", u->host);
			break;
		}
		r = read_timed(fd, doc + total, MAXDOC + (int)sizeof(hdr) - total);
		if (r == -2) {
			setstatus("timed out reading from ", u->host);
			break;
		}
		if (r <= 0) break;
		total += r;
	}
	close(fd);
	doc[total] = 0;
	body = NULL;
	for (n = 0; n + 3 < total && n + 4 < (int)sizeof(hdr); n++)
		if (doc[n] == '\r' && doc[n + 1] == '\n' && doc[n + 2] == '\r' && doc[n + 3] == '\n') {
			body = doc + n + 4;
			break;
		}
	if (!body)
		for (n = 0; n + 1 < total && n + 2 < (int)sizeof(hdr); n++)
			if (doc[n] == '\n' && doc[n + 1] == '\n') {
				body = doc + n + 2;
				break;
			}
	if (!body || strncmp(doc, "HTTP/", 5)) {
		free(doc);
		setstatus("not an HTTP response from ", u->host);
		return NULL;
	}
	hl = (int)(body - doc);
	memcpy(hdr, doc, (unsigned)hl);
	hdr[hl] = 0;
	{
		const char *sp = strchr(hdr, ' ');
		*code = sp ? atoi(sp + 1) : 0;
	}
	header_value(hdr, "Content-Type", ctype, 64);
	header_value(hdr, "Location", location, MAXURL);
	*len = total - hl;
	if (*len > MAXDOC) *len = MAXDOC;
	memmove(doc, body, (unsigned)*len);
	doc[*len] = 0;
	return doc;
}

static char *fetch(const char *want, char *final, int *len, char *ctype) {
	char cur[MAXURL], location[MAXURL];
	int i, code = 0;
	Url u;
	clean_url(want, cur, MAXURL);
	for (i = 0; i <= MAXREDIR; i++) {
		char *doc;
		if (parse_url(cur, &u) < 0) {
			setstatus("cannot use the address ", cur);
			return NULL;
		}
		normalize(u.path);
		url_string(&u, final);
		ctype[0] = 0;
		if (!strcmp(u.scheme, "file")) {
			struct stat st;
			doc = read_file(u.path, len, ctype);
			if (!doc) setstatus("cannot read ", u.path);
			else if (stat(u.path, &st) == 0 && S_ISDIR(st.st_mode) && final[strlen(final) - 1] != '/' && strlen(final) + 1 < MAXURL) strcat(final, "/");
			return doc;
		}
		location[0] = 0;
		doc = http_get(&u, len, ctype, location, &code);
		if (!doc) return NULL;
		if ((code == 301 || code == 302 || code == 303 || code == 307 || code == 308) && location[0]) {
			char next[MAXURL];
			free(doc);
			if (resolve(final, location, next) < 0 || strncmp(next, "http://", 7)) {
				setstatus("refused a redirection to ", location);
				return NULL;
			}
			strcpy(cur, next);
			continue;
		}
		if (code >= 400) {
			char c[16];
			snprintf(c, sizeof(c), "%d", code);
			setstatus("the server answered ", c);
		}
		return doc;
	}
	setstatus("too many redirections at ", cur);
	return NULL;
}

static char lb[512];
static short la[512];
static int lbn, indent, pending_space, pre, curlink = -1, blanks = 1, skip, intitle, utf8;
static char word[512];
static short wattr[512];
static int wlen;
static int liststack[16], listcount[16], nlist;

static void store(const char *s, const short *a, int n) {
	if (page.nlines >= MAXLINES) return;
	if (page.textlen + n + 1 > page.textcap) {
		int cap = page.textcap ? page.textcap * 2 : 65536;
		while (cap < page.textlen + n + 1) cap *= 2;
		page.text = realloc(page.text, (unsigned)cap);
		page.attr = realloc(page.attr, (unsigned)cap * sizeof(short));
		if (!page.text || !page.attr) exit(1);
		page.textcap = cap;
	}
	page.lines[page.nlines++] = page.textlen;
	memcpy(page.text + page.textlen, s, (unsigned)n);
	memcpy(page.attr + page.textlen, a, (unsigned)n * sizeof(short));
	page.textlen += n;
	page.lines[page.nlines] = page.textlen;
}

static void newline(void) {
	int n = lbn;
	while (n > 0 && lb[n - 1] == ' ' && la[n - 1] == 0) n--;
	store(lb, la, n);
	blanks = n == 0 ? blanks + 1 : 0;
	lbn = 0;
	while (lbn < indent && lbn < width - 10) {
		lb[lbn] = ' ';
		la[lbn++] = 0;
	}
}

static void put(char c, short a) {
	if (lbn >= width) newline();
	lb[lbn] = c;
	la[lbn++] = a;
}

static int line_empty(void) {
	int i;
	for (i = 0; i < lbn; i++)
		if (lb[i] != ' ') return 0;
	return 1;
}

static void flush_word(void) {
	int i, need;
	if (!wlen) return;
	need = wlen + (pending_space && !line_empty() ? 1 : 0);
	if (lbn + need > width && !line_empty()) newline();
	if (pending_space && !line_empty()) put(' ', lbn > 0 && la[lbn - 1] && la[lbn - 1] == wattr[0] ? wattr[0] : 0);
	for (i = 0; i < wlen; i++) put(word[i], wattr[i]);
	wlen = 0;
	pending_space = 0;
}

static void block(int n) {
	flush_word();
	pending_space = 0;
	if (!line_empty()) newline();
	else lbn = 0;
	while (blanks < n) newline();
	lbn = 0;
	while (lbn < indent && lbn < width - 10) {
		lb[lbn] = ' ';
		la[lbn++] = 0;
	}
}

static void text_char(int c) {
	short a = (short)(curlink >= 0 ? curlink + 1 : 0);
	if (skip) return;
	if (intitle) {
		int n = (int)strlen(page.title);
		if (c < 32 || c > 126) c = ' ';
		if (n < (int)sizeof(page.title) - 1 && !(c == ' ' && (n == 0 || page.title[n - 1] == ' '))) {
			page.title[n] = (char)c;
			page.title[n + 1] = 0;
		}
		return;
	}
	if (c >= 128) {
		if (utf8 > 0 && (c & 0xC0) == 0x80) {
			utf8--;
			return;
		}
		utf8 = (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : 0;
		c = '?';
	} else utf8 = 0;
	if (pre) {
		if (c == '\n') {
			newline();
			return;
		}
		if (c == '\t') {
			do put(' ', a);
			while (lbn % 8);
			return;
		}
		if (c == '\r' || c < 32 || c == 127) return;
		put((char)c, a);
		return;
	}
	if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f') {
		flush_word();
		pending_space = 1;
		return;
	}
	if (c < 32 || c == 127) return;
	if (wlen >= width - indent - 1 || wlen >= (int)sizeof(word) - 1) flush_word();
	word[wlen] = (char)c;
	wattr[wlen++] = a;
}

static void text_str(const char *s) {
	for (; *s; s++) text_char((unsigned char)*s);
}

static const struct {
	const char *name;
	const char *text;
} entities[] = {
	{"amp", "&"}, {"lt", "<"}, {"gt", ">"}, {"quot", "\""}, {"apos", "'"}, {"nbsp", " "},
	{"copy", "(c)"}, {"reg", "(R)"}, {"trade", "(TM)"}, {"mdash", "--"}, {"ndash", "-"},
	{"hellip", "..."}, {"laquo", "<<"}, {"raquo", ">>"}, {"middot", "*"}, {"bull", "*"},
	{"times", "x"}, {"lsquo", "'"}, {"rsquo", "'"}, {"ldquo", "\""}, {"rdquo", "\""}, {"deg", " deg"}
};

static const char *entity(const char *p, char *out) {
	char name[12];
	int n = 0, i;
	unsigned v = 0;
	if (p[1] == '#') {
		const char *q = p + 2;
		int hex = *q == 'x' || *q == 'X';
		if (hex) q++;
		while (n < 8 && (hex ? isxdigit((unsigned char)*q) : isdigit((unsigned char)*q))) {
			v = v * (hex ? 16 : 10) + (unsigned)(isdigit((unsigned char)*q) ? *q - '0' : tolower((unsigned char)*q) - 'a' + 10);
			q++;
			n++;
		}
		if (!n) return NULL;
		if (*q == ';') q++;
		if (v >= 32 && v < 127) {
			out[0] = (char)v;
			out[1] = 0;
		} else strcpy(out, v == 160 ? " " : v == 9 || v == 10 ? " " : "?");
		return q;
	}
	for (p++; n < 10 && isalnum((unsigned char)p[n]); n++) name[n] = p[n];
	name[n] = 0;
	for (i = 0; i < (int)(sizeof(entities) / sizeof(entities[0])); i++)
		if (!strcmp(entities[i].name, name)) {
			strcpy(out, entities[i].text);
			p += n;
			if (*p == ';') p++;
			return p;
		}
	return NULL;
}

static void decode(const char *in, int n, char *out, int max) {
	int k = 0;
	const char *end = in + n;
	while (in < end && k < max - 4) {
		char e[8];
		const char *nx;
		if (*in == '&' && (nx = entity(in, e)) != NULL && nx <= end) {
			int l = (int)strlen(e);
			if (k + l >= max - 1) break;
			memcpy(out + k, e, (unsigned)l);
			k += l;
			in = nx;
		} else out[k++] = *in++;
	}
	out[k] = 0;
}

static int get_attr(const char *tag, int tlen, const char *name, char *out, int max) {
	const char *p = tag, *end = tag + tlen;
	int l = (int)strlen(name);
	while (p < end && !isspace((unsigned char)*p)) p++;
	while (p < end) {
		const char *an, *vs;
		int al, vl = 0;
		char q = 0;
		while (p < end && (isspace((unsigned char)*p) || *p == '/')) p++;
		an = p;
		while (p < end && !isspace((unsigned char)*p) && *p != '=' && *p != '/') p++;
		al = (int)(p - an);
		while (p < end && isspace((unsigned char)*p)) p++;
		vs = p;
		if (p < end && *p == '=') {
			p++;
			while (p < end && isspace((unsigned char)*p)) p++;
			if (p < end && (*p == '"' || *p == '\'')) q = *p++;
			vs = p;
			while (p < end && (q ? *p != q : !isspace((unsigned char)*p))) p++;
			vl = (int)(p - vs);
			if (q && p < end) p++;
		}
		if (al == l && !strncasecmp(an, name, (unsigned)l)) {
			decode(vs, vl, out, max);
			return 0;
		}
		if (al == 0) p++;
	}
	out[0] = 0;
	return -1;
}

static void link_mark(int n) {
	char m[16];
	int i;
	snprintf(m, sizeof(m), "[%d]", n + 1);
	for (i = 0; m[i]; i++) text_char((unsigned char)m[i]);
}

static int add_link(const char *href) {
	char abs[MAXURL];
	int r;
	if (page.nlinks >= MAXLINKS) return -1;
	r = resolve(page.url, href, abs);
	if (r == -2) {
		char b[MAXURL];
		snprintf(b, sizeof(b), "refused:%s", abs[0] ? abs : href);
		page.links[page.nlinks] = strdup(b);
	} else if (r < 0) {
		char b[MAXURL], c[MAXURL];
		clean_url(href, c, MAXURL);
		snprintf(b, sizeof(b), "unsupported:%s", c);
		page.links[page.nlinks] = strdup(b);
	} else page.links[page.nlinks] = strdup(abs);
	return page.links[page.nlinks] ? page.nlinks++ : -1;
}

static int is(const char *name, int nl, const char *want) {
	return (int)strlen(want) == nl && !strncasecmp(name, want, (unsigned)nl);
}

static void tag(const char *t, int tlen) {
	int close = 0, nl = 0;
	const char *name;
	char v[MAXURL];
	if (tlen > 0 && t[0] == '/') {
		close = 1;
		t++;
		tlen--;
	}
	name = t;
	while (nl < tlen && (isalnum((unsigned char)name[nl]))) nl++;
	if (is(name, nl, "title")) {
		intitle = !close;
		return;
	}
	if (skip) return;
	if (is(name, nl, "p") || is(name, nl, "table") || is(name, nl, "dl") || is(name, nl, "form")) block(1);
	else if (is(name, nl, "h1") || is(name, nl, "h2") || is(name, nl, "h3") || is(name, nl, "h4") || is(name, nl, "h5") || is(name, nl, "h6")) {
		block(1);
		if (!close && (name[1] == '1' || name[1] == '2')) {
			text_str(name[1] == '1' ? "== " : "-- ");
		}
	} else if (is(name, nl, "br")) {
		flush_word();
		pending_space = 0;
		newline();
	} else if (is(name, nl, "div") || is(name, nl, "tr") || is(name, nl, "dt") || is(name, nl, "section") || is(name, nl, "article") || is(name, nl, "header") || is(name, nl, "footer") || is(name, nl, "nav") || is(name, nl, "main") || is(name, nl, "center") || is(name, nl, "address") || is(name, nl, "figure") || is(name, nl, "caption")) block(0);
	else if (is(name, nl, "dd")) {
		block(0);
		if (!close) text_str("    ");
	} else if (is(name, nl, "ul") || is(name, nl, "ol") || is(name, nl, "menu")) {
		block(nlist ? 0 : 1);
		if (!close && nlist < 16) {
			liststack[nlist] = name[0] == 'o' || name[0] == 'O';
			listcount[nlist++] = 0;
			indent += 3;
		} else if (close && nlist > 0) {
			nlist--;
			indent -= 3;
			if (indent < 0) indent = 0;
		}
		block(nlist ? 0 : 1);
	} else if (is(name, nl, "li")) {
		block(0);
		if (!close) {
			char m[16];
			if (nlist && liststack[nlist - 1]) snprintf(m, sizeof(m), "%d.", ++listcount[nlist - 1]);
			else strcpy(m, "*");
			lbn = indent >= 3 ? indent - 3 : 0;
			text_str(m);
			flush_word();
			pending_space = 1;
		}
	} else if (is(name, nl, "blockquote")) {
		block(1);
		indent += close ? -4 : 4;
		if (indent < 0) indent = 0;
		block(1);
	} else if (is(name, nl, "pre") || is(name, nl, "listing") || is(name, nl, "xmp")) {
		block(1);
		pre += close ? -1 : 1;
		if (pre < 0) pre = 0;
	} else if (is(name, nl, "hr")) {
		int i;
		block(0);
		for (i = indent; i < width - 1; i++) put('-', 0);
		newline();
	} else if (is(name, nl, "td") || is(name, nl, "th")) {
		flush_word();
		if (!close && !line_empty()) {
			text_str(" | ");
		}
	} else if (is(name, nl, "a")) {
		if (close) {
			flush_word();
			curlink = -1;
		} else if (get_attr(t, tlen, "href", v, sizeof(v)) == 0) {
			int n = add_link(v);
			flush_word();
			if (n >= 0) {
				if (pending_space && !line_empty()) {
					put(' ', 0);
					pending_space = 0;
				}
				curlink = n;
				link_mark(n);
			}
		}
	} else if (is(name, nl, "img") && !close) {
		get_attr(t, tlen, "alt", v, 200);
		text_char('[');
		text_str(v[0] ? v : "image");
		text_char(']');
	} else if ((is(name, nl, "iframe") || is(name, nl, "frame")) && !close && get_attr(t, tlen, "src", v, sizeof(v)) == 0) {
		int n = add_link(v);
		if (n >= 0) {
			int old = curlink;
			block(0);
			curlink = n;
			link_mark(n);
			text_str("frame");
			flush_word();
			curlink = old;
			block(0);
		}
	} else if ((is(name, nl, "input") || is(name, nl, "button") || is(name, nl, "select")) && !close) {
		char ty[32];
		get_attr(t, tlen, "type", ty, sizeof(ty));
		if (!strcasecmp(ty, "hidden")) return;
		get_attr(t, tlen, "value", v, 60);
		text_char('[');
		text_str(v[0] ? v : is(name, nl, "select") ? "choice" : ty[0] ? ty : "input");
		text_char(']');
	}
}

static int raw_end(const char *p, const char *end, const char *name) {
	int l = (int)strlen(name);
	return end - p > l + 1 && p[0] == '<' && p[1] == '/' && !strncasecmp(p + 2, name, (unsigned)l);
}

static const char *raw_tag(const char *t, const char *end) {
	int n = 0;
	while (t + n < end && isalnum((unsigned char)t[n])) n++;
	if (t + n < end && t[n] != '>' && !isspace((unsigned char)t[n]) && t[n] != '/') return NULL;
	if (n == 6 && !strncasecmp(t, "script", 6)) return "script";
	if (n == 5 && !strncasecmp(t, "style", 5)) return "style";
	return NULL;
}

static void reset_render(void) {
	lbn = indent = pending_space = pre = skip = intitle = utf8 = wlen = nlist = 0;
	curlink = -1;
	blanks = 1;
}

static void render_html(const char *doc, int len) {
	const char *p = doc, *end = doc + len;
	reset_render();
	while (p < end) {
		if (*p == '<') {
			const char *q;
			if (end - p >= 4 && !strncmp(p, "<!--", 4)) {
				q = p + 4;
				while (q + 2 < end && strncmp(q, "-->", 3)) q++;
				p = q + 3 > end ? end : q + 3;
				continue;
			}
			if (p + 1 < end && (isalpha((unsigned char)p[1]) || p[1] == '/' || p[1] == '!' || p[1] == '?')) {
				char q2 = 0;
				q = p + 1;
				while (q < end && (q2 || *q != '>')) {
					if (q2 && *q == q2) q2 = 0;
					else if (!q2 && (*q == '"' || *q == '\'') && q > p + 1 && (q[-1] == '=' || isspace((unsigned char)q[-1]))) q2 = *q;
					q++;
				}
				if (p[1] != '!' && p[1] != '?') tag(p + 1, (int)(q - p - 1));
				if (q < end && q[-1] != '/' && raw_tag(p + 1, q)) {
					const char *nm = raw_tag(p + 1, q);
					q++;
					while (q < end && !raw_end(q, end, nm)) q++;
					while (q < end && *q != '>') q++;
				}
				p = q < end ? q + 1 : end;
				continue;
			}
		}
		if (*p == '&') {
			char e[8];
			const char *nx = entity(p, e);
			if (nx && nx <= end) {
				text_str(e);
				p = nx;
				continue;
			}
		}
		text_char((unsigned char)*p++);
	}
	flush_word();
	if (!line_empty()) newline();
	while (page.nlines > 0 && page.lines[page.nlines] == page.lines[page.nlines - 1]) page.nlines--;
}

static void render_text(const char *doc, int len) {
	int i;
	reset_render();
	pre = 1;
	for (i = 0; i < len; i++) text_char((unsigned char)doc[i]);
	if (lbn) newline();
	while (page.nlines > 0 && page.lines[page.nlines] == page.lines[page.nlines - 1]) page.nlines--;
}

static void free_page(void) {
	int i;
	for (i = 0; i < page.nlinks; i++) free(page.links[i]);
	page.nlinks = 0;
	page.nlines = 0;
	page.textlen = 0;
	page.title[0] = 0;
	page.note[0] = 0;
}

static int load(const char *url) {
	char final[MAXURL], ctype[64];
	char *doc;
	int len = 0;
	status[0] = 0;
	doc = fetch(url, final, &len, ctype);
	if (!doc) return -1;
	free_page();
	strncpy(page.url, final, MAXURL - 1);
	page.url[MAXURL - 1] = 0;
	if (!strncasecmp(ctype, "text/html", 9) || !ctype[0]) render_html(doc, len);
	else if (!strncasecmp(ctype, "text/", 5)) render_text(doc, len);
	else {
		char m[200];
		int i;
		for (i = 0; ctype[i]; i++)
			if (!isalnum((unsigned char)ctype[i]) && !strchr("/-+.;= ", ctype[i])) ctype[i] = '?';
		snprintf(m, sizeof(m), "<p>This document is of type %s, %d bytes, which browse does not show.", ctype, len);
		render_html(m, (int)strlen(m));
	}
	if (len >= MAXDOC) snprintf(page.note, sizeof(page.note), "the document was cut at %d bytes", MAXDOC);
	free(doc);
	return 0;
}

static void print_line(int i, int sel) {
	int s = page.lines[i], e = page.lines[i + 1], k, inv = 0;
	for (k = s; k < e; k++) {
		int want = sel >= 0 && page.attr[k] == sel + 1;
		if (!dump && want != inv) {
			fputs(want ? "\033[7m" : "\033[0m", stdout);
			inv = want;
		}
		putchar(page.text[k]);
	}
	if (inv) fputs("\033[0m", stdout);
}

static void do_dump(void) {
	int i;
	for (i = 0; i < page.nlines; i++) {
		print_line(i, -1);
		putchar('\n');
	}
	if (page.nlinks) {
		printf("\nReferences\n\n");
		for (i = 0; i < page.nlinks; i++) printf("%4d. %s\n", i + 1, page.links[i]);
	}
	if (page.note[0]) printf("\n[%s]\n", page.note);
}

static int link_line(int n) {
	int i, k;
	for (i = 0; i < page.nlines; i++)
		for (k = page.lines[i]; k < page.lines[i + 1]; k++)
			if (page.attr[k] == n + 1) return i;
	return -1;
}

static void put_clipped(const char *s, int max) {
	int n = 0;
	for (; *s && n < max; s++, n++) putchar(*s >= 32 && *s < 127 ? *s : '?');
}

static void draw(int top, int cur) {
	int i, body = rows - 3;
	char head[512];
	fputs("\033[H\033[2J\033[7m", stdout);
	snprintf(head, sizeof(head), " %s", page.title[0] ? page.title : page.url);
	put_clipped(head, width);
	for (i = (int)strlen(head); i < width; i++) putchar(' ');
	fputs("\033[0m\r\n", stdout);
	for (i = 0; i < body; i++) {
		if (top + i < page.nlines) print_line(top + i, cur);
		fputs("\r\n", stdout);
	}
	if (status[0]) put_clipped(status, width - 1);
	else if (cur >= 0) put_clipped(page.links[cur], width - 1);
	else if (page.note[0]) put_clipped(page.note, width - 1);
	fputs("\r\n", stdout);
	put_clipped("up/down: link  enter: follow  left: back  space: page  g: go  ?: help  q: quit", width - 1);
	fflush(stdout);
}

static int key(void) {
	unsigned char c;
	if (read(0, &c, 1) != 1) return -1;
	if (c == 27) {
		unsigned char a, b;
		struct pollfd pf;
		pf.fd = 0;
		pf.events = POLLIN;
		if (poll(&pf, 1, 5) <= 0 || read(0, &a, 1) != 1) return 27;
		if (a != '[' && a != 'O') return 27;
		if (read(0, &b, 1) != 1) return 27;
		if (b == 'A') return 'K';
		if (b == 'B') return 'J';
		if (b == 'C') return 'L';
		if (b == 'D') return 'H';
		if (b == '5' || b == '6') {
			unsigned char t;
			read(0, &t, 1);
			return b == '5' ? '-' : ' ';
		}
		return 27;
	}
	return c;
}

static int prompt(const char *what, char *buf, int max) {
	int n = 0;
	buf[0] = 0;
	for (;;) {
		int c;
		printf("\033[%d;1H\033[K", rows - 1);
		put_clipped(what, width / 2);
		put_clipped(buf, width - (int)strlen(what) - 2);
		fflush(stdout);
		c = key();
		if (c < 0 || c == 27 || c == 7) return -1;
		if (c == '\r' || c == '\n') return n ? 0 : -1;
		if ((c == 8 || c == 127) && n > 0) buf[--n] = 0;
		else if (c >= 32 && c < 127 && n < max - 1) {
			buf[n++] = (char)c;
			buf[n] = 0;
		}
	}
}

static void push_hist(int top, int cur) {
	if (nhist == MAXHIST) {
		memmove(hist, hist + 1, sizeof(Hist) * (MAXHIST - 1));
		nhist--;
	}
	strcpy(hist[nhist].url, page.url);
	hist[nhist].top = top;
	hist[nhist].cur = cur;
	nhist++;
}

static int first_link_from(int top, int dir) {
	int i, k, body = rows - 3;
	if (dir > 0) {
		for (i = top; i < page.nlines && i < top + body; i++)
			for (k = page.lines[i]; k < page.lines[i + 1]; k++)
				if (page.attr[k]) return page.attr[k] - 1;
	} else {
		for (i = top + body - 1; i >= top; i--)
			if (i < page.nlines)
				for (k = page.lines[i + 1] - 1; k >= page.lines[i]; k--)
					if (page.attr[k]) return page.attr[k] - 1;
	}
	return -1;
}

static void go(const char *url, int *top, int *cur, int record) {
	char save[MAXURL];
	int ot = *top, oc = *cur;
	strcpy(save, page.url);
	setstatus("loading ", url);
	draw(*top, -1);
	if (load(url) < 0) {
		if (save[0] && strcmp(save, page.url)) load(save);
		*top = ot;
		*cur = oc;
		return;
	}
	if (record && save[0]) {
		char tmp[MAXURL];
		strcpy(tmp, page.url);
		strcpy(page.url, save);
		push_hist(ot, oc);
		strcpy(page.url, tmp);
	}
	*top = 0;
	*cur = first_link_from(0, 1);
	if (!status[0]) status[0] = 0;
}

static void follow(int n, int *top, int *cur) {
	const char *l;
	if (n < 0 || n >= page.nlinks) {
		setstatus("there is no such link", NULL);
		return;
	}
	l = page.links[n];
	if (!strncmp(l, "unsupported:", 12)) {
		setstatus("browse cannot follow ", l + 12);
		return;
	}
	if (!strncmp(l, "refused:", 8)) {
		setstatus("a page from the network may not open a local file: ", l + 8);
		return;
	}
	go(l, top, cur, 1);
}

static int find(const char *what, int from) {
	int i, k, l = (int)strlen(what);
	for (i = from; i < page.nlines; i++)
		for (k = page.lines[i]; k + l <= page.lines[i + 1]; k++)
			if (!strncasecmp(page.text + k, what, (unsigned)l)) return i;
	return -1;
}

static const char *help_page =
	"<title>browse: help</title><h1>browse</h1>"
	"<p>A text browser for TORTOISE. It shows HTML and plain text, follows http: and file: links and never runs scripts.</p>"
	"<ul><li>Up and down arrows, k and j: the previous and next link"
	"<li>Space, + and Page Down: the next page; - and Page Up: the previous one"
	"<li>Enter, Right arrow and l: follow the link; a number and Enter: follow that link"
	"<li>Left arrow, Backspace and u: go back"
	"<li>g: go to an address; r: reload; =: show the address of the page and the link"
	"<li>/: find text; n: find it again"
	"<li>? and h: this help; q: quit</ul>";

static void interact(const char *start) {
	struct termios t;
	struct winsize ws;
	int top = 0, cur = -1, num = 0, c;
	char buf[MAXURL], last[128];
	last[0] = 0;
	if (ioctl(0, TIOCGWINSZ, &ws) == 0 && ws.ws_col >= 40 && ws.ws_row >= 10) {
		width = ws.ws_col > 250 ? 250 : ws.ws_col;
		rows = ws.ws_row;
	}
	if (tcgetattr(0, &saved) == 0) {
		t = saved;
		t.c_lflag &= ~(ICANON | ECHO);
		t.c_cc[VMIN] = 1;
		t.c_cc[VTIME] = 0;
		tcsetattr(0, TCSANOW, &t);
		raw = 1;
	}
	signal(SIGINT, on_signal);
	signal(SIGTERM, on_signal);
	signal(SIGHUP, on_signal);
	signal(SIGPIPE, SIG_IGN);
	page.url[0] = 0;
	if (load(start) < 0) {
		char msg[400];
		snprintf(msg, sizeof(msg), "<title>browse</title><p>%s</p><p>Press g to go to an address or q to quit.</p>", status);
		free_page();
		strncpy(page.url, "about:error", MAXURL);
		render_html(msg, (int)strlen(msg));
	}
	cur = first_link_from(0, 1);
	for (;;) {
		int body = rows - 3;
		draw(top, cur);
		status[0] = 0;
		c = key();
		if (c < 0 || c == 'q') break;
		if (c >= '0' && c <= '9') {
			num = num * 10 + (c - '0');
			if (num > 100000) num = 0;
			snprintf(status, sizeof(status), "link %d", num);
			continue;
		}
		if ((c == '\r' || c == '\n') && num) {
			follow(num - 1, &top, &cur);
			num = 0;
			continue;
		}
		num = 0;
		switch (c) {
		case 'J':
		case 'j':
			if (cur >= 0 && cur + 1 < page.nlinks && link_line(cur + 1) >= 0) {
				int l = link_line(cur + 1);
				if (l < top + body) cur++;
				else if (top + body < page.nlines) {
					top = l - body + 1 > top + body ? top + body : l - body + 1;
					if (link_line(cur + 1) < top + body) cur++;
				}
			} else if (top + body < page.nlines) top++;
			break;
		case 'K':
		case 'k':
			if (cur > 0 && link_line(cur - 1) >= top) cur--;
			else if (cur > 0 && link_line(cur - 1) >= 0) {
				top = link_line(cur - 1);
				cur--;
			} else if (top > 0) top--;
			break;
		case ' ':
		case '+':
			if (top + body < page.nlines) {
				top += body;
				cur = first_link_from(top, 1);
			}
			break;
		case '-':
		case 'b':
			top = top > body ? top - body : 0;
			cur = first_link_from(top, 1);
			break;
		case 'L':
		case 'l':
		case '\r':
		case '\n':
			if (cur >= 0) follow(cur, &top, &cur);
			break;
		case 'H':
		case 'u':
		case 8:
		case 127:
			if (nhist > 0) {
				Hist h = hist[--nhist];
				if (load(h.url) == 0) {
					top = h.top < page.nlines ? h.top : 0;
					cur = h.cur < page.nlinks ? h.cur : first_link_from(top, 1);
				}
			} else setstatus("this is the first page", NULL);
			break;
		case 'g':
			if (prompt("Go to: ", buf, sizeof(buf)) == 0) {
				char abs[MAXURL];
				if (strstr(buf, "://") || !strncmp(buf, "file:", 5)) strcpy(abs, buf);
				else snprintf(abs, sizeof(abs), "http://%s", buf);
				go(abs, &top, &cur, 1);
			}
			break;
		case 'r':
			{
				char u[MAXURL];
				strcpy(u, page.url);
				go(u, &top, &cur, 0);
			}
			break;
		case '=':
			snprintf(status, sizeof(status), "%s  (%d lines, %d links)", page.url, page.nlines, page.nlinks);
			break;
		case '/':
			if (prompt("Find: ", last, sizeof(last)) < 0) break;
		case 'n':
			if (last[0]) {
				int l = find(last, c == '/' ? top : top + 1);
				if (l < 0) setstatus("not found: ", last);
				else {
					top = l;
					cur = first_link_from(top, 1);
				}
			}
			break;
		case '?':
		case 'h':
			free_page();
			if (page.url[0]) push_hist(top, cur);
			strcpy(page.url, "about:help");
			render_html(help_page, (int)strlen(help_page));
			top = 0;
			cur = -1;
			break;
		default:
			break;
		}
	}
	restore_tty();
}

int main(int argc, char **argv) {
	int i = 1;
	page.lines = malloc(sizeof(int) * (MAXLINES + 1));
	if (!page.lines) return 1;
	while (i < argc && argv[i][0] == '-') {
		if (!strcmp(argv[i], "-dump")) dump = 1;
		else if (!strcmp(argv[i], "-width") && i + 1 < argc) {
			width = atoi(argv[++i]);
			if (width < 20) width = 20;
			if (width > 250) width = 250;
		} else {
			fprintf(stderr, "usage: browse [-dump] [-width n] [url]\n");
			return 1;
		}
		i++;
	}
	if (dump) {
		if (i >= argc) {
			fprintf(stderr, "usage: browse -dump [-width n] url\n");
			return 1;
		}
		if (load(argv[i]) < 0) {
			fprintf(stderr, "browse: %s\n", status);
			return 1;
		}
		do_dump();
		return 0;
	}
	interact(i < argc ? argv[i] : "file:/var/www/index.html");
	return 0;
}
