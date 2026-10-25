#include "chos/user.h"
static volatile uint8_t large_bss[256u*1024u];
static const volatile uint8_t readonly[4097]={0x43,0x48,0x4f,0x53,[4096]=0xa5};
static volatile uint32_t writable_code=0xd65f03c0u; /* AArch64 RET, mapped RW/XN. */
static int fail(void){const char *s="[FAIL] native ELF runtime\n";chos_write(s,chos_strlen(s));return 1;}
int main(int argc,char **argv){
    if(argc<1 || !argv || !argv[0])return fail();
    if(readonly[0]!=0x43 || readonly[4096]!=0xa5 || writable_code!=0xd65f03c0u)return fail();
    for(size_t i=0;i<sizeof(large_bss);i++)if(large_bss[i])return fail();
    for(size_t i=0;i<sizeof(large_bss);i++)large_bss[i]=(uint8_t)i;
    for(size_t i=0;i<sizeof(large_bss);i++)if(large_bss[i]!=(uint8_t)i)return fail();
    if(argc>1 && chos_strcmp(argv[1],"readonly")==0){
        __asm__ volatile("strb %w1,[%0]"::"r"(readonly),"r"(0):"memory");return fail();
    }
    if(argc>1 && chos_strcmp(argv[1],"execute")==0){
        ((void(*)(void))(uintptr_t)&writable_code)();return fail();
    }
    if(argc>1 && chos_strcmp(argv[1],"gap")==0){
        *(volatile uint32_t *)UINT64_C(0x20030000)=1;return fail();
    }
    if(argc>2 && chos_strcmp(argv[1],"kernel")==0){
        uint64_t a=0;size_t n=0;
        for(;argv[2][n];++n){unsigned char c=(unsigned char)argv[2][n];unsigned v;
            if(c>='0'&&c<='9')v=c-'0';else if(c>='a'&&c<='f')v=c-'a'+10u;else return fail();
            if(n>=16u)return fail();a=(a<<4)|v;
        }
        if(!n || !a)return fail();
        volatile uint32_t forbidden=*(volatile uint32_t *)(uintptr_t)a;(void)forbidden;return fail();
    }
    const char *s="[ OK ] native AArch64 ELF RX/R/RW, sparse image, large BSS and arguments passed\n";
    chos_write(s,chos_strlen(s));return 0;
}
