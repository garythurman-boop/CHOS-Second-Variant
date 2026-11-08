#include <setjmp.h>
#include <stdint.h>
#include <limits.h>
#include <pthread.h>
#include <errno.h>
#include "chos/user.h"
extern void chos_test_setjmp_registers(jmp_buf,uint64_t *,int);
#define CHECK(x) do{if(!(x)){static const char text[]="[FAIL] native setjmp: " #x "\n";chos_write(text,sizeof(text)-1);return 1;}}while(0)
static void descend(jmp_buf env,int value,unsigned depth) __attribute__((noinline,noreturn));
static void descend(jmp_buf env,int value,unsigned depth) {
    volatile unsigned char padding[512];
    for(unsigned i=0;i<sizeof(padding);++i)padding[i]=(unsigned char)(i+depth);
    chos_sleep_ticks(1);
    if(depth)descend(env,value,depth-1);
    /* These writes must remain visible: a jump restores registers,not objects
     * or errno. The inaccessible deeper frames are abandoned by stack restore. */
    errno=EIO;
    if(padding[0]!=0)longjmp(env,99);
    longjmp(env,value);
}
static int run(void) {
    const int values[]={0,1,-7,INT_MAX,INT_MIN};
    for(unsigned n=0;n<sizeof(values)/sizeof(*values);++n){
        jmp_buf env;uint64_t got[22]={0};chos_test_setjmp_registers(env,got,values[n]);
        for(unsigned i=0;i<11;++i){
            static const unsigned tags[]={0x1919,0x2020,0x2121,0x2222,0x2323,0x2424,0x2525,0x2626,0x2727,0x2828,0x2929};
            CHECK(got[i]==((uint64_t)tags[i]<<48|tags[i]));}
        static const unsigned fp[]={0x0808,0x0909,0x1010,0x1111,0x1212,0x1313,0x1414,0x1515};
        for(unsigned i=0;i<8;++i)CHECK(got[12+i]==((uint64_t)fp[i]<<48|fp[i]));
        CHECK((got[11]&15u)==0&&got[21]==1);
        CHECK((int32_t)got[20]==(values[n]?values[n]:1));
        volatile unsigned seen=0;
        int code=setjmp(env);
        if(!code){seen=17;descend(env,values[n],6);}
        CHECK(code==(values[n]?values[n]:1)&&seen==17&&errno==EIO);
    }
    jmp_buf outer,inner;volatile unsigned reached=0;
    if(!setjmp(outer)){
        int v=_setjmp(inner);
        if(!v)_longjmp(inner,-33);
        CHECK(v==-33);reached=9;longjmp(outer,5);
    }
    CHECK(reached==9);
    return 0;
}
static void *worker(void *unused){(void)unused;return (void *)(uintptr_t)run();}
int main(void) {
    CHECK(sizeof(jmp_buf)==176);CHECK(!run());
    pthread_t threads[3];
    for(unsigned i=0;i<3;++i)CHECK(!pthread_create(&threads[i],0,worker,0));
    for(unsigned i=0;i<3;++i){void *result=0;CHECK(!pthread_join(threads[i],&result)&&!result);}
    const char text[]="[ OK ] native AArch64 nonlocal jumps restore GPR,FP,SP,nested flow and independent threads\n";chos_write(text,sizeof(text)-1);return 0;
}
