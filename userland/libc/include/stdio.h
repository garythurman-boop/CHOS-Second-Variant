#ifndef CHOS_LIBC_STDIO_H
#define CHOS_LIBC_STDIO_H
#if defined(CHOS_HOST_TEST) && !defined(CHOS_FILENAME_HOST_TEST) && !defined(CHOS_STDIO_HOST_TEST)
#include_next <stdio.h>
#else
#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>
#include "chos/file_api.h"
/* Read-only binary snapshots and unbuffered console stdout/stderr. Writable
 * ordinary files, stdin and line/full output buffering remain unprovided. */
typedef struct __chos_FILE FILE;
typedef struct { int64_t __position; } fpos_t;
#define EOF (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define FOPEN_MAX CHOS_FILE_OPEN_MAX
#define BUFSIZ 256
#define _IOFBF 0
#define _IOLBF 1
#define _IONBF 2
#ifdef __cplusplus
extern "C" {
#endif
extern FILE *const stdout;
extern FILE *const stderr;
int printf(const char *,...);
int vprintf(const char *,va_list);
int fprintf(FILE *,const char *,...);
int vfprintf(FILE *,const char *,va_list);
size_t fwrite(const void *,size_t,size_t,FILE *);
int fputc(int,FILE *);
int putc(int,FILE *);
int putchar(int);
int fputs(const char *,FILE *);
int puts(const char *);
int setvbuf(FILE *,char *,int,size_t);
int snprintf(char *,size_t,const char *,...);
int vsnprintf(char *,size_t,const char *,va_list);
FILE *fopen(const char *, const char *);
int fclose(FILE *);
size_t fread(void *, size_t, size_t, FILE *);
int fgetc(FILE *);
int getc(FILE *);
char *fgets(char *, int, FILE *);
int ungetc(int, FILE *);
int fseek(FILE *, long, int);
long ftell(FILE *);
int fgetpos(FILE *, fpos_t *);
int fsetpos(FILE *, const fpos_t *);
void rewind(FILE *);
int feof(FILE *);
int ferror(FILE *);
void clearerr(FILE *);
int fflush(FILE *);
int fileno(FILE *);
int remove(const char *);
/* CHFS refuses replacement of an existing destination. */
int rename(const char *,const char *);
#ifdef __cplusplus
}
#endif
#endif
#endif
