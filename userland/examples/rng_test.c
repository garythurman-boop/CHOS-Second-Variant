/* Live syscall regression: compiled by test-userland and run from a disposable
 * CHFS image by tests/qemu_platform.py. Never prints entropy bytes. */
#include "chos/user.h"
static const char readonly_byte[]="read-only";
static int failure(void) {
    static const char message[]="[FAIL] EL0 RNG contract\n";
    chos_write(message,sizeof(message)-1);return 1;
}
int main(int argc,char **argv) {
    unsigned char buffer[CHOS_SYS_MAX_RANDOM_BYTES+2];
    int absent=argc>1 && !chos_strcmp(argv[1],"missing");
    if(chos_random_bytes(0,0)!=0 ||
       chos_random_bytes(buffer,CHOS_SYS_MAX_RANDOM_BYTES+1)!=-CHOS_ERRNO_E2BIG ||
       chos_random_bytes(0,1)!=-CHOS_ERRNO_EFAULT ||
       chos_random_bytes((void *)readonly_byte,1)!=-CHOS_ERRNO_EFAULT ||
       chos_random_bytes((void *)(uintptr_t)UINT64_C(0xfffffffffffffff0),32)!=-CHOS_ERRNO_EFAULT) return failure();
    long amount=-CHOS_ERRNO_EAGAIN;
    for(unsigned tries=0;tries<50;++tries) {
        for(unsigned i=0;i<sizeof(buffer);++i)buffer[i]=0xa5;
        amount=chos_random_bytes(buffer+1,CHOS_SYS_MAX_RANDOM_BYTES);
        if(absent && amount!=-CHOS_ERRNO_EAGAIN)return failure();
        if(amount>0 || (absent && tries==4))break;
        if(amount!=-CHOS_ERRNO_EAGAIN)return failure();
        chos_sleep_ticks(1);
    }
    if(!absent && (amount<=0 || amount>CHOS_SYS_MAX_RANDOM_BYTES))return failure();
    if(buffer[0]!=0xa5 || buffer[sizeof(buffer)-1]!=0xa5)return failure();
    if(amount>0)for(unsigned i=(unsigned)amount+1;i<sizeof(buffer);++i)if(buffer[i]!=0xa5)return failure();
    const char *message=absent?"[ OK ] EL0 RNG validation and missing-device failure passed\n":"[ OK ] EL0 RNG validation and hardware receive passed\n";
    chos_write(message,chos_strlen(message));return 0;
}
