#include <chos/user.h>
#include <chos/libc.h>
#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) { printf("[FAIL] GUI screen line %u\n",__LINE__); return 1; } } while(0)
static chos_gui_screen_info_t expected;
static long query(void *out,uint64_t size,uint64_t flags) {
    return chos_syscall3(CHOS_SYS_GUI_SCREEN_INFO,(long)(uintptr_t)out,(long)size,(long)flags);
}
static int rejected(int denied) {
    struct { uint64_t before; chos_gui_screen_info_t info; uint64_t after; } value,saved;
    memset(&value,0xa5,sizeof(value));saved=value;
    if(denied) {
        CHECK(query(&value.info,sizeof(value.info),0)==-CHOS_ERRNO_EPERM);
        CHECK(!memcmp(&value,&saved,sizeof(value)));
        puts("[ OK ] GUI screen capability refusal preserved output");
        return 0;
    }
    uint64_t sizes[]={0,1,63,65,UINT64_MAX};
    for(unsigned i=0;i<sizeof(sizes)/sizeof(sizes[0]);++i) {
        CHECK(query(&value.info,sizes[i],0)==-CHOS_ERRNO_EINVAL);
        CHECK(!memcmp(&value,&saved,sizeof(value)));
    }
    CHECK(query(&value.info,sizeof(value.info),1)==-CHOS_ERRNO_EINVAL);
    CHECK(query(&value.info,sizeof(value.info),UINT64_MAX)==-CHOS_ERRNO_EINVAL);
    CHECK(!memcmp(&value,&saved,sizeof(value)));
    CHECK(query(NULL,sizeof(value.info),0)==-CHOS_ERRNO_EFAULT);
    CHECK(query((void *)(uintptr_t)(UINTPTR_MAX-31),sizeof(value.info),0)==-CHOS_ERRNO_EFAULT);
    static const chos_gui_screen_info_t readonly={0};
    CHECK(query((void *)&readonly,sizeof(readonly),0)==-CHOS_ERRNO_EFAULT);
    long page=getpagesize();CHECK(page>0);
    unsigned char *mapping=mmap(NULL,(size_t)page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    CHECK(mapping!=MAP_FAILED);
    memset(mapping+page-32,0xa5,32);
    CHECK(!mprotect(mapping+page,(size_t)page,PROT_NONE));
    CHECK(query(mapping+page-32,64,0)==-CHOS_ERRNO_EFAULT);
    for(unsigned i=0;i<32;++i)CHECK(mapping[page-32+i]==0xa5);
    CHECK(!munmap(mapping,(size_t)page*2));
    CHECK(!query(&value.info,sizeof(value.info),0));
    CHECK(value.before==saved.before && value.after==saved.after);
    CHECK(!memcmp(&value.info,&expected,sizeof(expected)));
    puts("[ OK ] GUI screen size flags pointers and protected outputs passed");
    return 0;
}
static void *worker(void *unused) {
    (void)unused;
    for(unsigned i=0;i<64;++i) {
        chos_gui_screen_info_t info;
        if(chos_gui_screen_info(&info)||memcmp(&info,&expected,sizeof(info)))return (void*)1;
    }
    return NULL;
}
int main(int argc,char **argv) {
    if(argc==2&&!strcmp(argv[1],"denied"))return rejected(1);
    CHECK(!chos_gui_screen_info(&expected));
    CHECK(expected.size_bytes==sizeof(expected) && expected.version==CHOS_GUI_SCREEN_VERSION);
    CHECK(expected.pixel_format==CHOS_GUI_SCREEN_XRGB8888 && !expected.reserved);
    CHECK(!expected.x && !expected.y && !expected.available_x && !expected.available_y);
    CHECK(expected.width>=800 && expected.height>=600 && expected.available_width==expected.width);
    CHECK(expected.scale_numerator>=1 && expected.scale_numerator<=3 && expected.scale_denominator==1);
    CHECK(expected.available_height==expected.height-30*expected.scale_numerator);
    CHECK(expected.bits_per_pixel==32 && expected.color_bits==24);
    printf("[SCREEN] width=%u height=%u available=%u scale=%u/%u bpp=%u color=%u\n",expected.width,expected.height,expected.available_height,expected.scale_numerator,expected.scale_denominator,expected.bits_per_pixel,expected.color_bits);
    CHECK(!rejected(0));
    pthread_t threads[3];pthread_attr_t attr;
    CHECK(!pthread_attr_init(&attr) && !pthread_attr_setstacksize(&attr,128*1024));
    for(unsigned i=0;i<3;++i)CHECK(!pthread_create(&threads[i],&attr,worker,NULL));
    for(unsigned i=0;i<3;++i){void *result=NULL;CHECK(!pthread_join(threads[i],&result) && !result);}
    CHECK(!pthread_attr_destroy(&attr));
    long caps=chos_capabilities();CHECK(caps>0);
    long child=chos_spawn_limited("/SCREEN.ELF","denied",(uint64_t)caps&~CHOS_PROCESS_CAP(CHOS_SYS_GUI_SUBMIT));
    CHECK(child>0 && !chos_wait((uint32_t)child));
    chos_heap_stats_t stats;CHECK(!chos_libc_heap_info(&stats) && !stats.allocations && !stats.mapped_bytes);
    puts("[ OK ] GUI screen workers capability child and heap cleaned up");
    return 0;
}
