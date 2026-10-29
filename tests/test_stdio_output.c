#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include "chos/syscall.h"
extern int host_printf(const char *,...) __asm__("printf");
extern int chos_host_readers(int (*)(void *),void *);
extern void chos_host_yield(void);
static _Thread_local int local_errno;
int *__chos_errno_location(void) { return &local_errno; }
#define CHECK(x) do { if (!(x)) { host_printf("output check failed line %d: %s\n",__LINE__,#x); abort(); } } while (0)
static unsigned char output[32768];
static size_t used,write_limit=SIZE_MAX,fail_after=SIZE_MAX;
static int write_error,oversize,zero,invalid_negative;
static unsigned calls;
int64_t chos_stdio_open(const char *p) { return !strcmp(p,"/fixture")?3:-ENOENT; }
int64_t chos_stdio_read(int fd,void *p,size_t n) { (void)p;(void)n;return fd==3?0:-EBADF; }
int64_t chos_stdio_seek(int fd,int64_t p,unsigned origin) { (void)origin;return fd==3?p:-EBADF; }
int64_t chos_stdio_close(int fd) { return fd==3?0:-EBADF; }
void chos_stdio_yield(void) { chos_host_yield(); }
int64_t chos_stdio_write(const void *p,size_t n) {
    CHECK(p&&n&&n<=CHOS_SYS_MAX_WRITE);++calls;
    if (invalid_negative) return INT64_MIN;
    if (zero) return 0;
    if (oversize) return (int64_t)n+1;
    if (write_error && used>=fail_after) return -write_error;
    if (n>write_limit) n=write_limit;
    if (used<fail_after && n>fail_after-used) n=fail_after-used;
    CHECK(n&&n<=sizeof(output)-used);memcpy(output+used,p,n);used+=n;return (int64_t)n;
}
static void reset(void) {
    used=0;calls=0;write_limit=SIZE_MAX;fail_after=SIZE_MAX;write_error=zero=oversize=invalid_negative=0;
    memset(output,0xcc,sizeof(output));clearerr(stdout);clearerr(stderr);
}
static void expect(const char *s) { CHECK(used==strlen(s)&&!memcmp(output,s,used)&&output[used]==0xcc); }
static int via_va(FILE *f,const char *format,...) { va_list a;va_start(a,format);int n=vfprintf(f,format,a);va_end(a);return n; }
static void formatting(void) {
    reset();CHECK(fileno(stdout)==1&&fileno(stderr)==2&&!fflush(NULL)&&!fflush(stdout));
    CHECK(!setvbuf(stdout,NULL,_IONBF,0));
    CHECK(setvbuf(stdout,NULL,_IOLBF,128)==-1&&errno==ENOTSUP);
    CHECK(setvbuf(stderr,NULL,_IOFBF,128)==-1&&errno==ENOTSUP);
    CHECK(setvbuf(stdout,NULL,4,0)==-1&&errno==EINVAL);
    CHECK(printf("%s:%04x:%lld:%.2f:%a","hi",42,LLONG_MIN,1.25,1.5)==(int)(sizeof("hi:002a:-9223372036854775808:1.25:0x1.8p+0")-1));
    expect("hi:002a:-9223372036854775808:1.25:0x1.8p+0");
    reset();write_limit=3;CHECK(via_va(stderr,"%2$d/%1$s","done",17)==7);expect("17/done");
    reset();CHECK(putchar(511)==255&&putc('x',stderr)=='x'&&!fputs("abc",stdout)&&!puts("def"));
    CHECK(used==9&&output[0]==255&&!memcmp(output+1,"xabcdef\n",8));
    reset();char large[1025];memset(large,'X',1024);large[1024]=0;write_limit=7;
    CHECK(fprintf(stderr,"[%s]",large)==1026&&used==1026&&output[0]=='['&&output[1025]==']'&&calls>100);
    reset();CHECK(fwrite("abcd",2,2,stdout)==2);expect("abcd");
    CHECK(!fwrite(NULL,0,SIZE_MAX,(FILE *)(uintptr_t)1));
    unsigned before=calls;CHECK(!fwrite(large,SIZE_MAX,2,stdout)&&errno==EOVERFLOW&&ferror(stdout)&&calls==before);
    clearerr(stdout);CHECK(!fwrite(NULL,1,1,stdout)&&errno==EINVAL&&ferror(stdout));
}
static void failures(void) {
    for (unsigned mode=0;mode<4;mode++) {
        reset();write_error=EACCES;fail_after=0;zero=mode==1;oversize=mode==2;invalid_negative=mode==3;
        CHECK(fprintf(stdout,"denied %d",17)==-1&&ferror(stdout)&&calls==1);
        CHECK(errno==(mode?EIO:EACCES));
    }
    reset();write_limit=3;write_error=EIO;fail_after=5;
    CHECK(fwrite("abcdefghij",2,5,stdout)==2&&used==5&&ferror(stdout)&&errno==EIO);
    reset();write_limit=3;write_error=EIO;fail_after=5;
    CHECK(fprintf(stdout,"[%s]","abcdefghij")==-1&&used==5&&ferror(stdout));
    reset();write_error=EIO;fail_after=0;CHECK(fputc('a',stderr)==EOF&&ferror(stderr));
    write_error=0;CHECK(fprintf(stderr,"recovered")==9&&ferror(stderr));
    clearerr(stderr);CHECK(!ferror(stderr));expect("recovered");
    reset();CHECK(fprintf(stderr,"%0$d",42)==-1&&errno==EINVAL&&!used&&ferror(stderr));
    clearerr(stderr);CHECK(fprintf(stderr,"%*d",INT_MIN,1)==-1&&errno==EOVERFLOW&&!used&&ferror(stderr));
    reset();write_error=EIO;fail_after=3;write_limit=2;
    CHECK(puts("abcdef")==EOF&&used==3&&!memcmp(output,"abc",3)&&ferror(stdout));
    reset();CHECK(fputs(NULL,stdout)==EOF&&errno==EINVAL&&ferror(stdout));
    reset();CHECK(via_va(stdout,NULL)==-1&&errno==EINVAL&&ferror(stdout));
}
static void directions(void) {
    reset();char b[8];CHECK(fread(b,1,1,stdout)==0&&ferror(stdout)&&errno==EBADF);
    clearerr(stdout);CHECK(!fgets(b,1,stdout)&&errno==EBADF&&ferror(stdout));
    CHECK(ungetc('a',stdout)==EOF&&errno==EBADF);
    CHECK(fseek(stdout,0,SEEK_SET)==-1&&errno==ESPIPE&&ftell(stdout)==-1&&errno==ESPIPE);
    FILE *f=fopen("/fixture","rb");CHECK(f);
    CHECK(!fwrite("x",1,1,f)&&errno==EBADF&&ferror(f));clearerr(f);
    CHECK(fprintf(f,"no")==-1&&errno==EBADF&&ferror(f));clearerr(f);
    CHECK(fputs("",f)==EOF&&errno==EBADF&&ferror(f));
    CHECK(setvbuf(f,NULL,_IONBF,0)==-1&&errno==ENOTSUP);CHECK(!fclose(f));
    CHECK(fputc('x',(FILE *)(uintptr_t)1)==EOF&&errno==EBADF);
}
static int writer(void *p) {
    (void)p;for (unsigned i=0;i<100;i++) CHECK(fprintf(stdout,"<%04u>\n",i)==7);return 0;
}
static void concurrency(void) {
    reset();write_limit=1;CHECK(!chos_host_readers(writer,NULL));
    CHECK(used==3*100*7);
    unsigned seen[100]={0};
    for (size_t n=0;n<used;n+=7) {
        CHECK(output[n]=='<'&&output[n+5]=='>'&&output[n+6]=='\n');
        unsigned value=0;
        for (unsigned j=1;j<=4;j++) { CHECK(output[n+j]>='0'&&output[n+j]<='9'); value=value*10+output[n+j]-'0'; }
        CHECK(value<100);++seen[value];
    }
    for (unsigned i=0;i<100;i++) CHECK(seen[i]==3);
}
int main(void) {
    formatting();failures();directions();concurrency();
    CHECK(!fclose(stdout)&&fputc('a',stdout)==EOF&&errno==EBADF);
    CHECK(!fclose(stderr)&&fprintf(stderr,"closed")==-1&&errno==EBADF);
    host_printf("native output stream checks passed\n");return 0;
}
