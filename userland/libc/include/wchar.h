#ifndef CHOS_LIBC_WCHAR_H
#define CHOS_LIBC_WCHAR_H
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <uchar.h>
#include <stdio.h>
#include <time.h>
typedef __WINT_TYPE__ wint_t;
#define WEOF ((wint_t)-1)
#ifndef WCHAR_MAX
#define WCHAR_MAX __WCHAR_MAX__
#endif
#ifndef WCHAR_MIN
#ifdef __WCHAR_UNSIGNED__
#define WCHAR_MIN 0
#else
#define WCHAR_MIN (-WCHAR_MAX-1)
#endif
#endif
#ifdef __cplusplus
extern "C" {
#endif
size_t wcslen(const wchar_t *);
size_t wcsnlen(const wchar_t *,size_t);
int wcscmp(const wchar_t *,const wchar_t *);
int wcsncmp(const wchar_t *,const wchar_t *,size_t);
wchar_t *wcscpy(wchar_t *,const wchar_t *);
wchar_t *wcsncpy(wchar_t *,const wchar_t *,size_t);
wchar_t *wcscat(wchar_t *,const wchar_t *);
wchar_t *wcsncat(wchar_t *,const wchar_t *,size_t);
wchar_t *wcschr(const wchar_t *,wchar_t);
wchar_t *wcsrchr(const wchar_t *,wchar_t);
wchar_t *wcspbrk(const wchar_t *,const wchar_t *);
size_t wcscspn(const wchar_t *,const wchar_t *);
size_t wcsspn(const wchar_t *,const wchar_t *);
wchar_t *wcsstr(const wchar_t *,const wchar_t *);
wchar_t *wcstok(wchar_t *,const wchar_t *,wchar_t **);
wchar_t *wcsdup(const wchar_t *);
wchar_t *wmemchr(const wchar_t *,wchar_t,size_t);
int wmemcmp(const wchar_t *,const wchar_t *,size_t);
wchar_t *wmemcpy(wchar_t *,const wchar_t *,size_t);
wchar_t *wmemmove(wchar_t *,const wchar_t *,size_t);
wchar_t *wmemset(wchar_t *,wchar_t,size_t);
int wcscoll(const wchar_t *,const wchar_t *);
size_t wcsxfrm(wchar_t *,const wchar_t *,size_t);
wint_t btowc(int);
int wctob(wint_t);
size_t mbrlen(const char *,size_t,mbstate_t *);
size_t mbrtowc(wchar_t *,const char *,size_t,mbstate_t *);
size_t wcrtomb(char *,wchar_t,mbstate_t *);
size_t mbsrtowcs(wchar_t *,const char **,size_t,mbstate_t *);
size_t wcsrtombs(char *,const wchar_t **,size_t,mbstate_t *);
/* Standard declarations needed by the C++ wrapper. Wide numeric/time formatting
 * and FILE I/O do not have implementations in this bounded profile; their
 * actual imports must fail at link time, including CMake executable probes. */
double wcstod(const wchar_t *,wchar_t **);
float wcstof(const wchar_t *,wchar_t **);
long double wcstold(const wchar_t *,wchar_t **);
long wcstol(const wchar_t *,wchar_t **,int);
long long wcstoll(const wchar_t *,wchar_t **,int);
unsigned long wcstoul(const wchar_t *,wchar_t **,int);
unsigned long long wcstoull(const wchar_t *,wchar_t **,int);
size_t wcsftime(wchar_t *,size_t,const wchar_t *,const struct tm *);
wint_t fgetwc(FILE *);
wchar_t *fgetws(wchar_t *,int,FILE *);
wint_t fputwc(wchar_t,FILE *);
int fputws(const wchar_t *,FILE *);
int fwide(FILE *,int);
int fwprintf(FILE *,const wchar_t *,...);
int fwscanf(FILE *,const wchar_t *,...);
wint_t getwc(FILE *);
wint_t getwchar(void);
wint_t putwc(wchar_t,FILE *);
wint_t putwchar(wchar_t);
int swprintf(wchar_t *,size_t,const wchar_t *,...);
int swscanf(const wchar_t *,const wchar_t *,...);
wint_t ungetwc(wint_t,FILE *);
int vfwprintf(FILE *,const wchar_t *,va_list);
int vfwscanf(FILE *,const wchar_t *,va_list);
int vswprintf(wchar_t *,size_t,const wchar_t *,va_list);
int vswscanf(const wchar_t *,const wchar_t *,va_list);
int vwprintf(const wchar_t *,va_list);
int vwscanf(const wchar_t *,va_list);
int wprintf(const wchar_t *,...);
int wscanf(const wchar_t *,...);
#ifdef __cplusplus
}
#endif
#endif
