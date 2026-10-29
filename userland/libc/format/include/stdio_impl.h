#ifndef CHOS_PRIVATE_FORMAT_IMPL_H
#define CHOS_PRIVATE_FORMAT_IMPL_H
#include <stddef.h>
#include <stdarg.h>
#include <errno.h>
#define EOF (-1)
#define F_ERR 32u
#define NL_ARGMAX 9
/* Every context is local to one formatter invocation, never a public FILE. */
typedef struct __chos_format_stream FILE;
struct __chos_format_stream {
    unsigned flags;
    unsigned char *wend,*wpos,*wbase,*buf;
    size_t buf_size;
    size_t (*write)(FILE *,const unsigned char *,size_t);
    int lock,lbf;
    void *cookie;
};
#define vfprintf __chos_format_vfprintf
#define strerror __chos_format_strerror
#define __strerror_l __chos_format_strerror_l
#define ferror(f) ((f)->flags & F_ERR)
/* Private stack contexts cannot be shared, so there is no stream lock owner. */
#define FLOCK(f) ((void)(f))
#define FUNLOCK(f) ((void)(f))
int vfprintf(FILE *,const char *,va_list);
int snprintf(char *,size_t,const char *,...);
int vsnprintf(char *,size_t,const char *,va_list);
char *strerror(int);
static inline int __towrite(FILE *f) {
    f->wbase=f->wpos=f->buf;f->wend=f->buf+f->buf_size;return 0;
}
static inline size_t __fwritex(const unsigned char *s,size_t size,FILE *f) {
    return f->write(f,s,size);
}
#endif
