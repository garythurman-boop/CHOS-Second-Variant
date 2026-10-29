/* Independent buffered read snapshots and unbuffered console output.
 * Registry/file locks yield on contention; no allocations or writable file modes. */
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <limits.h>
#include "chos/file_position.h"
#include "chos/syscall.h"
#ifdef CHOS_STDIO_HOST_TEST
extern int64_t chos_stdio_open(const char *);
extern int64_t chos_stdio_read(int, void *, size_t);
extern int64_t chos_stdio_seek(int, int64_t, unsigned);
extern int64_t chos_stdio_close(int);
extern void chos_stdio_yield(void);
extern int64_t chos_stdio_write(const void *,size_t);
#else
#include "chos/user.h"
#define chos_stdio_open chos_open
#define chos_stdio_read chos_read
#define chos_stdio_seek chos_seek
#define chos_stdio_close chos_close
#define chos_stdio_yield chos_yield
#define chos_stdio_write chos_write
#endif
struct __chos_FILE {
    uint32_t lock;
    int used, fd, eof, error, pushed, push_advance, output;
    unsigned char push, buffer[BUFSIZ];
    size_t at, end;
    uint64_t position;
};
static FILE streams[FOPEN_MAX];
static FILE console_out={.used=1,.fd=1,.output=1};
static FILE console_err={.used=1,.fd=2,.output=1};
FILE *const stdout=&console_out;
FILE *const stderr=&console_err;
extern int __chos_format_to_writer(long (*)(void *,const unsigned char *,size_t),
                                  void *,const char *,va_list);
static uint32_t registry_lock;
static void lock(uint32_t *word) {
    while (__atomic_exchange_n(word, 1, __ATOMIC_ACQUIRE)) chos_stdio_yield();
}
static void unlock(uint32_t *word) { __atomic_store_n(word, 0, __ATOMIC_RELEASE); }
static FILE *locked(FILE *f) {
    uintptr_t p=(uintptr_t)f, first=(uintptr_t)streams;
    if (f!=stdout && f!=stderr && (p<first || p-first>=sizeof(streams) || (p-first)%sizeof(FILE))) {
        errno=EBADF; return NULL;
    }
    lock(&f->lock);
    if (!f->used) { unlock(&f->lock); errno=EBADF; return NULL; }
    return f;
}
static int failed(FILE *f, int error) { f->error=1; errno=error; return EOF; }
static void discard(FILE *f) { f->at=f->end=0; f->pushed=0; }
FILE *fopen(const char *path, const char *mode) {
    if (!path || !mode) { errno=EINVAL; return NULL; }
    if (strcmp(mode,"r") && strcmp(mode,"rb")) { errno=ENOTSUP; return NULL; }
    lock(&registry_lock);
    for (unsigned i=0;i<FOPEN_MAX;i++) {
        FILE *f=&streams[i]; lock(&f->lock);
        if (f->used) { unlock(&f->lock); continue; }
        int64_t fd=chos_stdio_open(path);
        if (fd<0) { unlock(&f->lock); unlock(&registry_lock); errno=(int)-fd; return NULL; }
        f->fd=(int)fd; f->used=1; f->eof=f->error=0; f->position=0; discard(f);
        unlock(&f->lock); unlock(&registry_lock); return f;
    }
    unlock(&registry_lock); errno=EMFILE; return NULL;
}
int fclose(FILE *stream) {
    lock(&registry_lock); FILE *f=locked(stream);
    if (!f) { unlock(&registry_lock); return EOF; }
    int64_t r=f->output?0:chos_stdio_close(f->fd); f->used=0; discard(f);
    unlock(&f->lock); unlock(&registry_lock);
    if (r<0) { errno=(int)-r; return EOF; } return 0;
}
static int fill(FILE *f) {
    if (f->output) return failed(f,EBADF);
    if (f->eof) return 0;
    int64_t n=chos_stdio_read(f->fd,f->buffer,sizeof(f->buffer));
    if (n<0) return failed(f,(int)-n);
    if ((uint64_t)n>sizeof(f->buffer)) return failed(f,EIO);
    f->at=0; f->end=(size_t)n;
    if (!n) f->eof=1;
    return n ? 1 : 0;
}
static int byte(FILE *f) {
    if (f->pushed) {
        f->pushed=0; f->position+=(unsigned)f->push_advance; return f->push;
    }
    if (f->at==f->end && fill(f)<=0) return EOF;
    f->position++; return f->buffer[f->at++];
}
size_t fread(void *destination,size_t size,size_t count,FILE *stream) {
    if (!size || !count) return 0;
    FILE *f=locked(stream); if (!f) return 0;
    if (count>SIZE_MAX/size) { failed(f,EOVERFLOW); unlock(&f->lock); return 0; }
    if (!destination) { failed(f,EINVAL); unlock(&f->lock); return 0; }
    unsigned char *p=destination; size_t done=0,total=size*count;
    while (done<total) {
        if (f->pushed) { p[done++]=(unsigned char)byte(f); continue; }
        if (f->at==f->end && fill(f)<=0) break;
        size_t n=f->end-f->at; if (n>total-done) n=total-done;
        memcpy(p+done,f->buffer+f->at,n); f->at+=n; f->position+=n; done+=n;
    }
    unlock(&f->lock); return done/size;
}
int fgetc(FILE *stream) { FILE *f=locked(stream); if (!f) return EOF; int c=byte(f); unlock(&f->lock); return c; }
int getc(FILE *f) { return fgetc(f); }
char *fgets(char *out,int size,FILE *stream) {
    FILE *f=locked(stream); if (!f) return NULL;
    if (f->output) { failed(f,EBADF); unlock(&f->lock); return NULL; }
    if (!out || size<=0) { failed(f,EINVAL); unlock(&f->lock); return NULL; }
    int n=0,c=0;
    while (n<size-1 && (c=byte(f))!=EOF) { out[n++]=(char)c; if (c=='\n') break; }
    if (c==EOF && (!n || f->error)) { unlock(&f->lock); return NULL; }
    out[n]=0; unlock(&f->lock); return out;
}
int ungetc(int c,FILE *stream) {
    FILE *f=locked(stream); if (!f) return EOF;
    if (f->output) { failed(f,EBADF); unlock(&f->lock); return EOF; }
    if (c==EOF || f->pushed) { unlock(&f->lock); return EOF; }
    f->push=(unsigned char)c; f->pushed=1; f->push_advance=f->position>0;
    f->position-=(unsigned)f->push_advance; f->eof=0;
    unlock(&f->lock); return (unsigned char)c;
}
static int seek_locked(FILE *f,int64_t offset,int origin) {
    if (f->output) { errno=ESPIPE; return -1; }
    if (origin<SEEK_SET || origin>SEEK_END) { errno=EINVAL; return -1; }
    if (origin==SEEK_CUR) {
        uint64_t target; int r=chos_file_position(f->position,0,offset,CHOS_SEEK_CUR,&target);
        if (r) { errno=-r; return -1; } offset=(int64_t)target; origin=SEEK_SET;
    }
    int64_t r=chos_stdio_seek(f->fd,offset,(unsigned)origin);
    if (r<0) { errno=(int)-r; return -1; }
    f->position=(uint64_t)r; discard(f); f->eof=0; return 0;
}
int fseek(FILE *stream,long offset,int origin) {
    FILE *f=locked(stream); if (!f) return -1;
    int r=seek_locked(f,offset,origin); unlock(&f->lock); return r;
}
long ftell(FILE *stream) {
    FILE *f=locked(stream); if (!f) return -1;
    long r;
    if (f->output) { unlock(&f->lock); errno=ESPIPE; return -1; }
    if (f->position>LONG_MAX) { errno=EOVERFLOW; r=-1; } else r=(long)f->position;
    unlock(&f->lock); return r;
}
int fgetpos(FILE *f,fpos_t *out) {
    if (!out) { errno=EINVAL; return -1; }
    long r=ftell(f); if (r<0) return -1; out->__position=r; return 0;
}
int fsetpos(FILE *f,const fpos_t *in) {
    if (!in) { errno=EINVAL; return -1; }
    if (in->__position>LONG_MAX || in->__position<LONG_MIN) { errno=EOVERFLOW; return -1; }
    return fseek(f,(long)in->__position,SEEK_SET);
}
void rewind(FILE *stream) {
    FILE *f=locked(stream); if (!f) return;
    (void)seek_locked(f,0,SEEK_SET); f->error=0; unlock(&f->lock);
}
int feof(FILE *stream) { FILE *f=locked(stream); if (!f) return 0; int r=f->eof; unlock(&f->lock); return r; }
int ferror(FILE *stream) { FILE *f=locked(stream); if (!f) return 0; int r=f->error; unlock(&f->lock); return r; }
void clearerr(FILE *stream) { FILE *f=locked(stream); if (f) { f->eof=f->error=0; unlock(&f->lock); } }
int fflush(FILE *stream) {
    /* Console output is synchronous and unbuffered; no pending output on NULL. */
    if (!stream) return 0;
    FILE *f=locked(stream); if (!f) return EOF;
    if (f->output) { unlock(&f->lock); return 0; }
    int64_t r=chos_stdio_seek(f->fd,(int64_t)f->position,CHOS_SEEK_SET);
    if (r<0) { failed(f,(int)-r); unlock(&f->lock); return EOF; }
    discard(f); unlock(&f->lock); return 0;
}
int fileno(FILE *stream) { FILE *f=locked(stream); if (!f) return -1; int r=f->fd; unlock(&f->lock); return r; }

/* The caller holds the public stream lock for an entire operation. Both standard
 * output descriptors route to this process's native console writer. */
static long output_writer(void *context,const unsigned char *data,size_t size) {
    FILE *f=context;
    size_t request=size>CHOS_SYS_MAX_WRITE?CHOS_SYS_MAX_WRITE:size;
    int64_t n=chos_stdio_write(data,request);
    if (n<=0 || (uint64_t)n>request) {
        int error=n<0 && n>=-INT_MAX?(int)-n:EIO;
        failed(f,error); return -error;
    }
    return (long)n;
}
static size_t write_locked(FILE *f,const unsigned char *data,size_t size) {
    if (!f->output) { failed(f,EBADF); return 0; }
    size_t done=0;
    while (done<size) {
        long n=output_writer(f,data+done,size-done);
        if (n<0) break;
        done+=(size_t)n;
    }
    return done;
}
size_t fwrite(const void *data,size_t size,size_t count,FILE *stream) {
    if (!size || !count) return 0;
    FILE *f=locked(stream); if (!f) return 0;
    if (count>SIZE_MAX/size || !data) {
        failed(f,!data?EINVAL:EOVERFLOW); unlock(&f->lock); return 0;
    }
    size_t done=write_locked(f,data,size*count);
    unlock(&f->lock); return done/size;
}
int fputc(int c,FILE *stream) {
    unsigned char byte=(unsigned char)c;
    return fwrite(&byte,1,1,stream)==1?byte:EOF;
}
int putc(int c,FILE *stream) { return fputc(c,stream); }
int putchar(int c) { return fputc(c,stdout); }
int fputs(const char *text,FILE *stream) {
    FILE *f=locked(stream); if (!f) return EOF;
    if (!text) { failed(f,EINVAL); unlock(&f->lock); return EOF; }
    size_t n=strlen(text); size_t done=write_locked(f,(const unsigned char *)text,n);
    int result=done==n && f->output?0:EOF;
    unlock(&f->lock); return result;
}
int puts(const char *text) {
    FILE *f=locked(stdout); if (!f) return EOF;
    if (!text) { failed(f,EINVAL); unlock(&f->lock); return EOF; }
    size_t n=strlen(text);
    int result=write_locked(f,(const unsigned char *)text,n)==n &&
               write_locked(f,(const unsigned char *)"\n",1)==1?0:EOF;
    unlock(&f->lock); return result;
}
int vfprintf(FILE *stream,const char *format,va_list args) {
    FILE *f=locked(stream); if (!f) return -1;
    if (!format || !f->output) {
        failed(f,!format?EINVAL:EBADF); unlock(&f->lock); return -1;
    }
    int n=__chos_format_to_writer(output_writer,f,format,args);
    if (n<0) f->error=1;
    unlock(&f->lock); return n;
}
int fprintf(FILE *f,const char *format,...) {
    va_list args; va_start(args,format); int n=vfprintf(f,format,args); va_end(args); return n;
}
int vprintf(const char *format,va_list args) { return vfprintf(stdout,format,args); }
int printf(const char *format,...) {
    va_list args; va_start(args,format); int n=vprintf(format,args); va_end(args); return n;
}
int setvbuf(FILE *stream,char *buffer,int mode,size_t size) {
    (void)buffer; (void)size;
    FILE *f=locked(stream); if (!f) return -1;
    int error=(mode<_IOFBF || mode>_IONBF)?EINVAL:
              (!f->output || mode!=_IONBF)?ENOTSUP:0;
    unlock(&f->lock); if (error) { errno=error; return -1; } return 0;
}
