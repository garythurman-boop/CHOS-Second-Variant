#ifndef CHOS_LIBC_STDLIB_H
#define CHOS_LIBC_STDLIB_H
#include <stddef.h>
typedef struct {int quot,rem;} div_t;
typedef struct {long quot,rem;} ldiv_t;
typedef struct {long long quot,rem;} lldiv_t;
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#define MB_CUR_MAX ((size_t)4)
#ifdef __cplusplus
extern "C" {
#endif
/* Native spawn has no environment inheritance yet. Each process starts empty;
 * an application may supply its own NULL-terminated table through environ. */
extern char **environ;
char *getenv(const char *);
void *malloc(size_t);
void free(void *);
void *calloc(size_t,size_t);
void *realloc(void *,size_t);
void *aligned_alloc(size_t,size_t);
int posix_memalign(void **,size_t,size_t);
int atexit(void (*)(void));
void exit(int) __attribute__((noreturn));
void _Exit(int) __attribute__((noreturn));
void abort(void) __attribute__((noreturn));
int abs(int);
long labs(long);
long long llabs(long long);
div_t div(int,int);
ldiv_t ldiv(long,long);
lldiv_t lldiv(long long,long long);
long strtol(const char *,char **,int);
long long strtoll(const char *,char **,int);
unsigned long strtoul(const char *,char **,int);
unsigned long long strtoull(const char *,char **,int);
float strtof(const char *,char **);
double strtod(const char *,char **);
long double strtold(const char *,char **);
double atof(const char *);
int mblen(const char *,size_t);
int mbtowc(wchar_t *,const char *,size_t);
int wctomb(char *,wchar_t);
size_t mbstowcs(wchar_t *,const char *,size_t);
size_t wcstombs(char *,const wchar_t *,size_t);
int atoi(const char *);
long atol(const char *);
long long atoll(const char *);
void *bsearch(const void *,const void *,size_t,size_t,int (*)(const void *,const void *));
void qsort(void *,size_t,size_t,int (*)(const void *,const void *));
#ifdef __cplusplus
}
#endif
#endif
