/*
 * Copyright (C) 2026 Ivan Gaydardzhiev
 * Licensed under the GPL-3.0-only
 */

#ifndef _STDIO_H
#define _STDIO_H
#include <stddef.h>
#include <stdarg.h>
#define EOF (-1)
#define BUFSIZ 512
typedef struct FILE {
	int fd;
	int flags;
	int ungot;
	int rpos;
	int rlen;
	int wlen;
	char *buf;
} FILE;
extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
int putchar(int c);
int getchar(void);
int puts(const char *s);
int printf(const char *f, ...);
int fprintf(FILE *fp, const char *f, ...);
int sprintf(char *buf, const char *f, ...);
int snprintf(char *buf, size_t n, const char *f, ...);
int vprintf(const char *f, va_list ap);
int vfprintf(FILE *fp, const char *f, va_list ap);
int vsprintf(char *buf, const char *f, va_list ap);
int vsnprintf(char *buf, size_t n, const char *f, va_list ap);
FILE *fopen(const char *path, const char *mode);
int fclose(FILE *fp);
size_t fread(void *p, size_t size, size_t n, FILE *fp);
size_t fwrite(const void *p, size_t size, size_t n, FILE *fp);
char *fgets(char *s, int n, FILE *fp);
int fputs(const char *s, FILE *fp);
int fgetc(FILE *fp);
int fputc(int c, FILE *fp);
int getc(FILE *fp);
int putc(int c, FILE *fp);
int ungetc(int c, FILE *fp);
int fflush(FILE *fp);
int feof(FILE *fp);
int ferror(FILE *fp);
int remove(const char *path);
void perror(const char *s);
#define _IOFBF 0
#define _IOLBF 1
#define _IONBF 2
#define L_tmpnam 32
FILE *fdopen(int fd, const char *mode);
FILE *freopen(const char *path, const char *mode, FILE *fp);
FILE *tmpfile(void);
int fileno(FILE *fp);
long ftell(FILE *fp);
int fseek(FILE *fp, long off, int whence);
void rewind(FILE *fp);
void clearerr(FILE *fp);
int setvbuf(FILE *fp, char *buf, int mode, size_t size);
void setbuf(FILE *fp, char *buf);
int getline(char **line, size_t *cap, FILE *fp);
int sscanf(const char *s, const char *f, ...);
int fscanf(FILE *fp, const char *f, ...);
int scanf(const char *f, ...);
int vsscanf(const char *s, const char *f, va_list ap);
int vfscanf(FILE *fp, const char *f, va_list ap);
#endif
