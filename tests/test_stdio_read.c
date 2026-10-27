#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include <stdatomic.h>
extern int chos_host_readers(int (*)(void *),void *);
extern void chos_host_yield(void);
#include "chos/file_position.h"
extern int puts(const char *);
extern int printf(const char *,...);
static _Thread_local int local_errno;
int *__chos_errno_location(void) { return &local_errno; }
#define CHECK(x) do { if (!(x)) { printf("stdio check failed line %d: %s\n",__LINE__,#x); abort(); } } while (0)
static unsigned char data[8197];
static struct { int active; uint64_t position; } handles[FOPEN_MAX];
static atomic_flag backend_lock=ATOMIC_FLAG_INIT;
static void backend_acquire(void) { while (atomic_flag_test_and_set(&backend_lock)) chos_host_yield(); }
static void backend_release(void) { atomic_flag_clear(&backend_lock); }
static int open_error,read_error,seek_error,close_error,oversize;
static size_t read_limit=BUFSIZ;
static unsigned opens,reads,seeks,closes;
int64_t chos_stdio_open(const char *path) {
    backend_acquire(); opens++;
    if (open_error || strcmp(path,"/fixture")) { int r=open_error?open_error:ENOENT; backend_release(); return -r; }
    for (unsigned i=0;i<FOPEN_MAX;i++) if (!handles[i].active) { handles[i].active=1; handles[i].position=0; backend_release(); return i+3; }
    backend_release(); return -EMFILE;
}
int64_t chos_stdio_read(int fd,void *out,size_t n) {
    backend_acquire(); reads++;
    if (fd<3 || fd>=3+(int)FOPEN_MAX || !handles[fd-3].active) { backend_release(); return -EBADF; }
    if (read_error || oversize) { int64_t r=read_error?-read_error:(int64_t)n+1; backend_release(); return r; }
    uint64_t p=handles[fd-3].position;
    size_t available=p<sizeof(data)?sizeof(data)-(size_t)p:0;
    if (n>available) n=available; if (n>read_limit) n=read_limit;
    if (n) memcpy(out,data+p,n); handles[fd-3].position+=n;
    backend_release(); return (int64_t)n;
}
int64_t chos_stdio_seek(int fd,int64_t offset,unsigned origin) {
    backend_acquire(); seeks++;
    if (fd<3 || fd>=3+(int)FOPEN_MAX || !handles[fd-3].active) { backend_release(); return -EBADF; }
    if (seek_error) { backend_release(); return -seek_error; }
    uint64_t p; int r=chos_file_position(handles[fd-3].position,sizeof(data),offset,origin,&p);
    if (!r) handles[fd-3].position=p;
    backend_release(); return r?r:(int64_t)p;
}
int64_t chos_stdio_close(int fd) {
    backend_acquire(); closes++;
    if (fd<3 || fd>=3+(int)FOPEN_MAX || !handles[fd-3].active) { backend_release(); return -EBADF; }
    handles[fd-3].active=0; backend_release(); return -close_error;
}
void chos_stdio_yield(void) { chos_host_yield(); }
static FILE *fixture(void) { FILE *f=fopen("/fixture","rb"); CHECK(f); return f; }
static void limits(void) {
    unsigned before=opens;
    const char *bad[]={"w","wb","r+","a","rb+","rbb","","rbe"};
    for (unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++) { errno=0; CHECK(!fopen("/fixture",bad[i])&&errno==ENOTSUP); }
    CHECK(opens==before); CHECK(!fopen(NULL,"r")&&errno==EINVAL); CHECK(!fopen("/fixture",NULL)&&errno==EINVAL);
    CHECK(!fopen("/missing","r")&&errno==ENOENT);
    open_error=EIO; CHECK(!fopen("/fixture","r")&&errno==EIO); open_error=0;
    FILE *f[FOPEN_MAX]; for (unsigned i=0;i<FOPEN_MAX;i++) { f[i]=fixture(); CHECK(fileno(f[i])==3+(int)i); }
    CHECK(!fopen("/fixture","r")&&errno==EMFILE);
    CHECK(!fclose(f[1])); f[1]=fixture(); CHECK(fileno(f[1])==4);
    for (unsigned i=0;i<FOPEN_MAX;i++) CHECK(!fclose(f[i]));
    CHECK(fclose(f[0])==EOF&&errno==EBADF); CHECK(fgetc((FILE *)(uintptr_t)1)==EOF&&errno==EBADF);
    CHECK(fseek(NULL,0,SEEK_SET)==-1&&errno==EBADF); CHECK(!fflush(NULL));
}
static void buffering(void) {
    FILE *f=fixture(); unsigned char out[9000];
    CHECK(fread(out,1,1,f)==1&&out[0]==data[0]&&ftell(f)==1);
    CHECK(handles[fileno(f)-3].position==BUFSIZ); // Physical read-ahead differs.
    CHECK(!fseek(f,17,SEEK_CUR)&&ftell(f)==18&&fgetc(f)==data[18]);
    fpos_t p; CHECK(!fgetpos(f,&p)&&p.__position==19);
    CHECK(ungetc(511,f)==255&&ftell(f)==18); CHECK(ungetc('x',f)==EOF);
    CHECK(fgetc(f)==255&&ftell(f)==19&&fgetc(f)==data[19]);
    CHECK(!fsetpos(f,&p)&&fgetc(f)==data[19]);
    CHECK(!fseek(f,-7,SEEK_END)&&ftell(f)==(long)sizeof(data)-7);
    memset(out,0x55,sizeof(out)); CHECK(fread(out,4,3,f)==1&&ftell(f)==sizeof(data));
    CHECK(!memcmp(out,data+sizeof(data)-7,7)&&out[7]==0x55&&feof(f)&&!ferror(f));
    unsigned r=reads; CHECK(fgetc(f)==EOF&&reads==r); // Sticky EOF.
    CHECK(ungetc('!',f)=='!'&&!feof(f)&&ftell(f)==sizeof(data)-1);
    CHECK(fgetc(f)=='!'&&ftell(f)==sizeof(data)&&fgetc(f)==EOF&&feof(f));
    clearerr(f); CHECK(!feof(f)&&!ferror(f)); CHECK(fgetc(f)==EOF&&reads>r);
    CHECK(!fseek(f,INT64_MAX,SEEK_SET)&&ftell(f)==INT64_MAX&&fgetc(f)==EOF);
    CHECK(fseek(f,1,SEEK_CUR)==-1&&errno==EOVERFLOW&&ftell(f)==INT64_MAX);
    CHECK(fseek(f,INT64_MIN,SEEK_CUR)==-1&&errno==EINVAL&&ftell(f)==INT64_MAX);
    CHECK(fseek(f,-1,SEEK_SET)==-1&&errno==EINVAL&&ftell(f)==INT64_MAX);
    CHECK(fseek(f,0,3)==-1&&errno==EINVAL&&ftell(f)==INT64_MAX);
    rewind(f); CHECK(!ftell(f)&&!feof(f)&&!ferror(f));
    CHECK(ungetc('Z',f)=='Z'&&ftell(f)==0&&fgetc(f)=='Z'&&ftell(f)==0&&fgetc(f)==data[0]&&ftell(f)==1);
    CHECK(!fflush(f)&&handles[fileno(f)-3].position==1&&fgetc(f)==data[1]);
    CHECK(!fclose(f));
}
static void errors(void) {
    FILE *f=fixture(); unsigned char out[8]; unsigned r=reads;
    CHECK(!fread(NULL,0,SIZE_MAX,f)&&reads==r&&!ferror(f));
    CHECK(!fread(out,SIZE_MAX,2,f)&&errno==EOVERFLOW&&ferror(f)&&reads==r); clearerr(f);
    CHECK(!fread(NULL,1,1,f)&&errno==EINVAL&&ferror(f)); clearerr(f);
    read_error=EIO; CHECK(fgetc(f)==EOF&&errno==EIO&&ferror(f)&&!feof(f));
    read_error=0; clearerr(f); CHECK(fgetc(f)==data[0]);
    long p=ftell(f); seek_error=EIO; CHECK(fseek(f,100,SEEK_SET)==-1&&errno==EIO&&ftell(f)==p);
    CHECK(fgetc(f)==data[p]); CHECK(fflush(f)==EOF&&errno==EIO&&ferror(f));
    seek_error=0; rewind(f); CHECK(!ferror(f));
    oversize=1; CHECK(fgetc(f)==EOF&&errno==EIO&&ferror(f)); oversize=0; clearerr(f);
    read_limit=3; CHECK(fread(out,2,4,f)==4&&!memcmp(out,data,8)); read_limit=BUFSIZ;
    close_error=EIO; CHECK(fclose(f)==EOF&&errno==EIO); close_error=0;
    CHECK(fgetc(f)==EOF&&errno==EBADF); f=fixture(); CHECK(!fclose(f));
}
static void lines(void) {
    FILE *f=fixture(); char out[400];
    CHECK(fgets(out,1,f)==out&&!out[0]&&!ftell(f));
    CHECK(!fgets(out,0,f)&&errno==EINVAL); clearerr(f);
    CHECK(!fgets(NULL,4,f)&&errno==EINVAL); clearerr(f);
    CHECK(!fseek(f,10,SEEK_SET)); CHECK(fgets(out,sizeof(out),f)==out&&out[0]=='\n'&&!out[1]);
    CHECK(!fseek(f,sizeof(data)-1,SEEK_SET)); CHECK(fgets(out,sizeof(out),f)==out&&out[0]==(char)data[sizeof(data)-1]&&!out[1]&&feof(f));
    CHECK(!fgets(out,sizeof(out),f)); CHECK(!fclose(f));
}
static uint64_t rng=0x6789abcde;
static uint32_t random32(void) { rng^=rng<<13; rng^=rng>>7; rng^=rng<<17; return (uint32_t)rng; }
static void random_reads(void) {
    FILE *f=fixture(); size_t position=0; unsigned char out[800];
    for (unsigned i=0;i<40000;i++) {
        if (random32()%3==0) { position=random32()%(sizeof(data)+100); CHECK(!fseek(f,(long)position,SEEK_SET)); }
        else { size_t n=random32()%sizeof(out),want=position<sizeof(data)?sizeof(data)-position:0; if (want>n) want=n;
            CHECK(fread(out,1,n,f)==want&&!memcmp(out,data+(position<sizeof(data)?position:sizeof(data)),want)); position+=want; }
        CHECK(ftell(f)==(long)position);
    }
    CHECK(!fclose(f));
}
static FILE *shared;
static atomic_uint histogram[256];
static int worker(void *unused) { (void)unused; int c; while ((c=fgetc(shared))!=EOF) atomic_fetch_add(&histogram[c],1); return 0; }
static void threads(void) {
    shared=fixture(); CHECK(!chos_host_readers(worker,NULL));
    unsigned expected[256]={0}; for (unsigned i=0;i<sizeof(data);i++) expected[data[i]]++;
    for (unsigned i=0;i<256;i++) CHECK(atomic_load(&histogram[i])==expected[i]);
    CHECK(ftell(shared)==sizeof(data)&&!ferror(shared)&&feof(shared)); CHECK(!fclose(shared));
}
int main(void) {
    for (unsigned i=0;i<sizeof(data);i++) data[i]=(unsigned char)i;
    limits(); buffering(); errors(); lines(); random_reads(); threads();
    for (unsigned i=0;i<FOPEN_MAX;i++) CHECK(!handles[i].active);
    puts("stdio: buffering,logical seek,pushback,short/error IO,limits,40000 model operations and three concurrent readers passed"); return 0;
}
