#ifndef CHOS_LIBC_STDIO_H
#define CHOS_LIBC_STDIO_H
#if defined(CHOS_HOST_TEST) && !defined(CHOS_FILENAME_HOST_TEST) && !defined(CHOS_STDIO_HOST_TEST)
#include_next <stdio.h>
#else
#include <stddef.h>
#include <stdint.h>
#include "chos/file_api.h"
/* Bounded read-only binary streams. Formatting and output streams are not
 * provided; unsupported imports must fail at link time. */
typedef struct __chos_FILE FILE;
typedef struct { int64_t __position; } fpos_t;
#define EOF (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define FOPEN_MAX CHOS_FILE_OPEN_MAX
#define BUFSIZ 256
#ifdef __cplusplus
extern "C" {
#endif
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
