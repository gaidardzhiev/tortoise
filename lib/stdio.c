/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>

#define F_READ 1
#define F_WRITE 2
#define F_EOF 4
#define F_ERR 8
#define F_LINE 16
#define F_UNBUF 32
#define F_OPEN 64
#define MAXFILES 20

void __stdio_flush_all(void);

static char inbuf[BUFSIZ], outbuf[BUFSIZ];
static FILE files[MAXFILES] = {
	{0, F_READ | F_OPEN, -1, 0, 0, 0, inbuf},
	{1, F_WRITE | F_LINE | F_OPEN, -1, 0, 0, 0, outbuf},
	{2, F_WRITE | F_UNBUF | F_OPEN, -1, 0, 0, 0, NULL},
};

FILE *stdin = &files[0];
FILE *stdout = &files[1];
FILE *stderr = &files[2];

int fflush(FILE *fp) {
	int off = 0;
	if (!fp) {
		__stdio_flush_all();
		return 0;
	}
	while (off < fp->wlen) {
		int r = write(fp->fd, fp->buf + off, fp->wlen - off);
		if (r <= 0) {
			fp->flags |= F_ERR;
			fp->wlen = 0;
			return EOF;
		}
		off += r;
	}
	fp->wlen = 0;
	return 0;
}

void __stdio_flush_all(void) {
	int i;
	for (i = 0; i < MAXFILES; i++)
		if ((files[i].flags & F_OPEN) && (files[i].flags & F_WRITE)) fflush(&files[i]);
}

int fputc(int c, FILE *fp) {
	unsigned char ch = (unsigned char)c;
	if (!(fp->flags & F_WRITE)) return EOF;
	if ((fp->flags & F_UNBUF) || !fp->buf) {
		if (fp == stdout || fp == stderr) fflush(stdout);
		if (write(fp->fd, &ch, 1) != 1) {
			fp->flags |= F_ERR;
			return EOF;
		}
		return ch;
	}
	fp->buf[fp->wlen++] = (char)ch;
	if (fp->wlen >= BUFSIZ || ((fp->flags & F_LINE) && ch == '\n')) fflush(fp);
	return ch;
}

int putc(int c, FILE *fp) {
	return fputc(c, fp);
}

int putchar(int c) {
	return fputc(c, stdout);
}

int fputs(const char *s, FILE *fp) {
	while (*s)
		if (fputc(*s++, fp) == EOF) return EOF;
	return 0;
}

int puts(const char *s) {
	if (fputs(s, stdout) == EOF) return EOF;
	return fputc('\n', stdout) == EOF ? EOF : 0;
}

int fgetc(FILE *fp) {
	if (fp->ungot >= 0) {
		int c = fp->ungot;
		fp->ungot = -1;
		return c;
	}
	if (!(fp->flags & F_READ) || (fp->flags & F_EOF)) return EOF;
	if (fp == stdin) fflush(stdout);
	if (fp->rpos >= fp->rlen) {
		int r = read(fp->fd, fp->buf, BUFSIZ);
		if (r <= 0) {
			fp->flags |= r < 0 ? F_ERR : F_EOF;
			return EOF;
		}
		fp->rlen = r;
		fp->rpos = 0;
	}
	return (unsigned char)fp->buf[fp->rpos++];
}

int getc(FILE *fp) {
	return fgetc(fp);
}

int getchar(void) {
	return fgetc(stdin);
}

int ungetc(int c, FILE *fp) {
	if (c == EOF) return EOF;
	fp->ungot = (unsigned char)c;
	fp->flags &= ~F_EOF;
	return c;
}

char *fgets(char *s, int n, FILE *fp) {
	int i = 0;
	while (i < n - 1) {
		int c = fgetc(fp);
		if (c == EOF) break;
		s[i++] = (char)c;
		if (c == '\n') break;
	}
	if (i == 0) return NULL;
	s[i] = 0;
	return s;
}

int feof(FILE *fp) {
	return (fp->flags & F_EOF) != 0;
}

int ferror(FILE *fp) {
	return (fp->flags & F_ERR) != 0;
}

FILE *fopen(const char *path, const char *mode) {
	int i, fl = 0, fd;
	FILE *fp = NULL;
	for (i = 3; i < MAXFILES; i++)
		if (!(files[i].flags & F_OPEN)) {
			fp = &files[i];
			break;
		}
	if (!fp) return NULL;
	if (mode[0] == 'r') fl = strchr(mode, '+') ? O_RDWR : O_RDONLY;
	else if (mode[0] == 'w') fl = (strchr(mode, '+') ? O_RDWR : O_WRONLY) | O_CREAT | O_TRUNC;
	else if (mode[0] == 'a') fl = (strchr(mode, '+') ? O_RDWR : O_WRONLY) | O_CREAT | O_APPEND;
	else return NULL;
	fd = open(path, fl, 0666);
	if (fd < 0) return NULL;
	fp->buf = malloc(BUFSIZ);
	if (!fp->buf) {
		close(fd);
		return NULL;
	}
	fp->fd = fd;
	fp->ungot = -1;
	fp->rpos = fp->rlen = fp->wlen = 0;
	fp->flags = F_OPEN;
	if (mode[0] == 'r' || strchr(mode, '+')) fp->flags |= F_READ;
	if (mode[0] != 'r' || strchr(mode, '+')) fp->flags |= F_WRITE;
	return fp;
}

int fclose(FILE *fp) {
	int r = 0;
	if (!(fp->flags & F_OPEN)) return EOF;
	if (fp->flags & F_WRITE) r = fflush(fp);
	if (fp >= &files[3]) {
		if (close(fp->fd) < 0) r = EOF;
		free(fp->buf);
		fp->buf = NULL;
	}
	fp->flags = 0;
	return r;
}

size_t fread(void *p, size_t size, size_t n, FILE *fp) {
	char *d = p;
	size_t total = size * n, i;
	for (i = 0; i < total; i++) {
		int c = fgetc(fp);
		if (c == EOF) break;
		d[i] = (char)c;
	}
	return size ? i / size : 0;
}

size_t fwrite(const void *p, size_t size, size_t n, FILE *fp) {
	const char *s = p;
	size_t total = size * n, i;
	for (i = 0; i < total; i++)
		if (fputc(s[i], fp) == EOF) break;
	return size ? i / size : 0;
}

int remove(const char *path) {
	return unlink(path);
}

void perror(const char *s) {
	if (s && *s) fprintf(stderr, "%s: ", s);
	fprintf(stderr, "%s\n", strerror(errno));
}

typedef struct {
	char *buf;
	size_t cap;
	size_t len;
	FILE *fp;
} Out;

static void outc(Out *o, int c) {
	if (o->fp) fputc(c, o->fp);
	else if (o->len + 1 < o->cap) o->buf[o->len] = (char)c;
	o->len++;
}

static void pad(Out *o, int c, int n) {
	while (n-- > 0) outc(o, c);
}

static int doprintf(Out *o, const char *f, va_list ap) {
	for (; *f; f++) {
		int left = 0, zero = 0, width = 0, prec = -1, len, neg = 0, base = 10, upper = 0, i;
		char tmp[16];
		char *s;
		unsigned v;
		if (*f != '%') {
			outc(o, *f);
			continue;
		}
		f++;
		for (;; f++) {
			if (*f == '-') left = 1;
			else if (*f == '0') zero = 1;
			else if (*f == ' ' || *f == '+' || *f == '#') ;
			else break;
		}
		if (*f == '*') {
			width = va_arg(ap, int);
			if (width < 0) {
				left = 1;
				width = -width;
			}
			f++;
		}
		while (*f >= '0' && *f <= '9') width = width * 10 + *f++ - '0';
		if (*f == '.') {
			f++;
			prec = 0;
			if (*f == '*') {
				prec = va_arg(ap, int);
				f++;
			}
			while (*f >= '0' && *f <= '9') prec = prec * 10 + *f++ - '0';
		}
		while (*f == 'l' || *f == 'h' || *f == 'z') f++;
		switch (*f) {
		case 0:
			return (int)o->len;
		case '%':
			outc(o, '%');
			continue;
		case 'c':
			tmp[0] = (char)va_arg(ap, int);
			if (!left) pad(o, ' ', width - 1);
			outc(o, tmp[0]);
			if (left) pad(o, ' ', width - 1);
			continue;
		case 's':
			s = va_arg(ap, char *);
			if (!s) s = "(null)";
			len = (int)strlen(s);
			if (prec >= 0 && len > prec) len = prec;
			if (!left) pad(o, ' ', width - len);
			for (i = 0; i < len; i++) outc(o, s[i]);
			if (left) pad(o, ' ', width - len);
			continue;
		case 'd': case 'i': {
			int x = va_arg(ap, int);
			if (x < 0) {
				neg = 1;
				v = -(unsigned)x;
			} else {
				v = (unsigned)x;
			}
			break;
		}
		case 'u': v = va_arg(ap, unsigned); break;
		case 'x': v = va_arg(ap, unsigned); base = 16; break;
		case 'X': v = va_arg(ap, unsigned); base = 16; upper = 1; break;
		case 'o': v = va_arg(ap, unsigned); base = 8; break;
		case 'p':
			v = (unsigned)va_arg(ap, void *);
			base = 16;
			outc(o, '0');
			outc(o, 'x');
			width -= 2;
			break;
		default:
			outc(o, '%');
			outc(o, *f);
			continue;
		}
		len = 0;
		do {
			int d = (int)(v % base);
			tmp[len++] = (char)(d < 10 ? '0' + d : (upper ? 'A' : 'a') + d - 10);
			v /= base;
		} while (v);
		while (len < prec) tmp[len++] = '0';
		if (neg) width--;
		if (left) {
			if (neg) outc(o, '-');
			for (i = len - 1; i >= 0; i--) outc(o, tmp[i]);
			pad(o, ' ', width - len);
		} else if (zero && prec < 0) {
			if (neg) outc(o, '-');
			pad(o, '0', width - len);
			for (i = len - 1; i >= 0; i--) outc(o, tmp[i]);
		} else {
			pad(o, ' ', width - len);
			if (neg) outc(o, '-');
			for (i = len - 1; i >= 0; i--) outc(o, tmp[i]);
		}
	}
	return (int)o->len;
}

int vfprintf(FILE *fp, const char *f, va_list ap) {
	Out o;
	o.buf = NULL;
	o.cap = 0;
	o.len = 0;
	o.fp = fp;
	return doprintf(&o, f, ap);
}

int vprintf(const char *f, va_list ap) {
	return vfprintf(stdout, f, ap);
}

int vsnprintf(char *buf, size_t n, const char *f, va_list ap) {
	Out o;
	int r;
	o.buf = buf;
	o.cap = n;
	o.len = 0;
	o.fp = NULL;
	r = doprintf(&o, f, ap);
	if (n) buf[o.len < n ? o.len : n - 1] = 0;
	return r;
}

int vsprintf(char *buf, const char *f, va_list ap) {
	return vsnprintf(buf, 0x7FFFFFFF, f, ap);
}

int printf(const char *f, ...) {
	va_list ap;
	int r;
	va_start(ap, f);
	r = vfprintf(stdout, f, ap);
	va_end(ap);
	return r;
}

int fprintf(FILE *fp, const char *f, ...) {
	va_list ap;
	int r;
	va_start(ap, f);
	r = vfprintf(fp, f, ap);
	va_end(ap);
	return r;
}

int sprintf(char *buf, const char *f, ...) {
	va_list ap;
	int r;
	va_start(ap, f);
	r = vsnprintf(buf, 0x7FFFFFFF, f, ap);
	va_end(ap);
	return r;
}

int snprintf(char *buf, size_t n, const char *f, ...) {
	va_list ap;
	int r;
	va_start(ap, f);
	r = vsnprintf(buf, n, f, ap);
	va_end(ap);
	return r;
}

static FILE *slot(void) {
	int i;
	for (i = 3; i < MAXFILES; i++)
		if (!(files[i].flags & F_OPEN)) return &files[i];
	errno = EMFILE;
	return NULL;
}

static int mode_flags(const char *mode) {
	int f = F_OPEN;
	if (mode[0] == 'r' || strchr(mode, '+')) f |= F_READ;
	if (mode[0] != 'r' || strchr(mode, '+')) f |= F_WRITE;
	return f;
}

FILE *fdopen(int fd, const char *mode) {
	FILE *fp;
	if (fd < 0 || (mode[0] != 'r' && mode[0] != 'w' && mode[0] != 'a')) {
		errno = EINVAL;
		return NULL;
	}
	fp = slot();
	if (!fp) return NULL;
	fp->buf = malloc(BUFSIZ);
	if (!fp->buf) return NULL;
	fp->fd = fd;
	fp->ungot = -1;
	fp->rpos = fp->rlen = fp->wlen = 0;
	fp->flags = mode_flags(mode);
	return fp;
}

FILE *freopen(const char *path, const char *mode, FILE *fp) {
	int fd = fp->fd, fl, nfd;
	if (fp->flags & F_WRITE) fflush(fp);
	if (mode[0] == 'r') fl = strchr(mode, '+') ? O_RDWR : O_RDONLY;
	else if (mode[0] == 'w') fl = (strchr(mode, '+') ? O_RDWR : O_WRONLY) | O_CREAT | O_TRUNC;
	else if (mode[0] == 'a') fl = (strchr(mode, '+') ? O_RDWR : O_WRONLY) | O_CREAT | O_APPEND;
	else {
		errno = EINVAL;
		return NULL;
	}
	nfd = open(path, fl, 0666);
	if (nfd < 0) return NULL;
	if (fp->flags & F_OPEN) close(fd);
	if (nfd != fd) {
		dup2(nfd, fd);
		close(nfd);
	}
	fp->fd = fd;
	fp->ungot = -1;
	fp->rpos = fp->rlen = fp->wlen = 0;
	fp->flags = mode_flags(mode) | (fp->flags & (F_LINE | F_UNBUF));
	if (!fp->buf && !(fp->flags & F_UNBUF)) fp->buf = malloc(BUFSIZ);
	return fp;
}

int fileno(FILE *fp) {
	return fp->fd;
}

long ftell(FILE *fp) {
	int pos = lseek(fp->fd, 0, SEEK_CUR);
	if (pos < 0) return -1;
	pos -= fp->rlen - fp->rpos;
	if (fp->ungot >= 0) pos--;
	return pos + fp->wlen;
}

int fseek(FILE *fp, long off, int whence) {
	if (whence == SEEK_CUR) off += ftell(fp);
	if (fp->flags & F_WRITE) fflush(fp);
	fp->rpos = fp->rlen = 0;
	fp->ungot = -1;
	if (lseek(fp->fd, off, whence == SEEK_CUR ? SEEK_SET : whence) < 0) return -1;
	fp->flags &= ~F_EOF;
	return 0;
}

void rewind(FILE *fp) {
	fseek(fp, 0, SEEK_SET);
	fp->flags &= ~F_ERR;
}

void clearerr(FILE *fp) {
	fp->flags &= ~(F_EOF | F_ERR);
}

int setvbuf(FILE *fp, char *buf, int mode, size_t size) {
	if (mode < 0 || mode > 2) return -1;
	fflush(fp);
	fp->flags &= ~(F_LINE | F_UNBUF);
	if (mode == _IOLBF) fp->flags |= F_LINE;
	if (mode == _IONBF) fp->flags |= F_UNBUF;
	if (buf && size >= BUFSIZ) fp->buf = buf;
	else if (!fp->buf && mode != _IONBF) fp->buf = malloc(BUFSIZ);
	return 0;
}

void setbuf(FILE *fp, char *buf) {
	setvbuf(fp, buf, buf ? _IOFBF : _IONBF, BUFSIZ);
}

int getline(char **line, size_t *cap, FILE *fp) {
	size_t n = 0;
	int c;
	if (!*line || *cap == 0) {
		*cap = 64;
		*line = malloc(*cap);
		if (!*line) return -1;
	}
	while ((c = fgetc(fp)) != EOF) {
		if (n + 2 > *cap) {
			char *b = realloc(*line, *cap * 2);
			if (!b) return -1;
			*line = b;
			*cap *= 2;
		}
		(*line)[n++] = (char)c;
		if (c == '\n') break;
	}
	if (n == 0) return -1;
	(*line)[n] = 0;
	return (int)n;
}

