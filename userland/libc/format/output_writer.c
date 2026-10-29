/* Bridge the maintained formatter to an owned, locked caller stream. Public FILE
 * objects never share the private musl layout. Each context lives on this stack. */
#include "stdio_impl.h"
#include <limits.h>
struct output_cookie {
    long (*write)(void *,const unsigned char *,size_t);
    void *context;
};
static int send(FILE *f,const unsigned char *bytes,size_t count) {
    struct output_cookie *cookie=f->cookie;
    while (count) {
        long n=cookie->write(cookie->context,bytes,count);
        if (n<=0 || (unsigned long)n>count) {
            errno=n<0 && n>=-INT_MAX ? (int)-n : EIO;
            f->flags|=F_ERR; f->wpos=0; return -1;
        }
        bytes+=(size_t)n; count-=(size_t)n;
    }
    return 0;
}
static size_t output_write(FILE *f,const unsigned char *bytes,size_t count) {
    if (f->flags&F_ERR) return 0;
    size_t pending=(size_t)(f->wpos-f->wbase);
    if (send(f,f->wbase,pending) || send(f,bytes,count)) return 0;
    f->wpos=f->wbase=f->buf; return count;
}
int __chos_format_to_writer(long (*write)(void *,const unsigned char *,size_t),
                           void *context,const char *format,va_list args) {
    if (!write || !format) { errno=EINVAL; return -1; }
    unsigned char storage[1];
    struct output_cookie cookie={write,context};
    FILE f={.lbf=EOF,.write=output_write,.lock=-1,.buf=storage,.cookie=&cookie};
    return vfprintf(&f,format,args);
}
