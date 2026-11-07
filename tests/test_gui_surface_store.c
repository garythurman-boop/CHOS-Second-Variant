#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "gui_surface_store.h"
#include "chos/spinlock.h"
static gui_surface_store_t s;
static chos_spinlock_t lock=CHOS_SPINLOCK_INITIALIZER;
static unsigned first_pin,writer_done,observations,backpressure;
static uint64_t nonce;
static uint64_t now(void){struct timespec t;assert(!clock_gettime(CLOCK_MONOTONIC,&t));return (uint64_t)t.tv_sec*1000000000u+t.tv_nsec;}
static void fill(uint64_t id,unsigned width,unsigned height,uint32_t color) {
    chos_gui_surface_tile_t t={0};
    unsigned across=(width+127)/128;
    for(unsigned i=0;i<128;++i)t.pixels[i]=color;
    for(unsigned row=0;row<height;++row)for(unsigned column=0;column<across;++column){
        t.tile_index=row*across+column;t.pixel_count=width-column*128;if(t.pixel_count>128)t.pixel_count=128;
        assert(!gui_surface_tile(&s,id,&t));
    }
}
static void *writer(void *unused) {
    (void)unused;uint64_t deadline=now()+UINT64_C(10000000000);
    for(unsigned n=0;n<200;++n){
        chos_gui_surface_begin_t b={.width=128,.height=8};int result;
        do {
            uint64_t irq=chos_spin_lock_irqsave(&lock);
            result=gui_surface_begin(&s,nonce+1,&b);
            if(!result)++nonce;
            chos_spin_unlock_irqrestore(&lock,irq);
            assert(now()<deadline);
            if(result){assert(result==-CHOS_ERRNO_EAGAIN);++backpressure;sched_yield();}
        }while(result);
        chos_gui_surface_tile_t t={.pixel_count=128};
        for(unsigned i=0;i<128;++i)t.pixels[i]=(uint32_t)nonce;
        for(t.tile_index=0;t.tile_index<8;++t.tile_index){
            uint64_t irq=chos_spin_lock_irqsave(&lock);assert(!gui_surface_tile(&s,nonce,&t));chos_spin_unlock_irqrestore(&lock,irq);
        }
        uint64_t irq=chos_spin_lock_irqsave(&lock);assert(!gui_surface_present(&s,nonce));chos_spin_unlock_irqrestore(&lock,irq);
        if(!n)while(!__atomic_load_n(&first_pin,__ATOMIC_ACQUIRE)){assert(now()<deadline);sched_yield();}
    }
    __atomic_store_n(&writer_done,1,__ATOMIC_RELEASE);return NULL;
}
static void *reader(void *unused) {
    (void)unused;uint64_t deadline=now()+UINT64_C(10000000000);
    do {
        gui_surface_frame_t f;uint64_t irq=chos_spin_lock_irqsave(&lock);int ok=gui_surface_acquire(&s,&f);chos_spin_unlock_irqrestore(&lock,irq);
        if(ok){
            __atomic_store_n(&first_pin,1,__ATOMIC_RELEASE);
            /* Hold a retired frame while the writer presents the other buffer. */
            struct timespec pause={0,1000000};nanosleep(&pause,NULL);
            for(unsigned i=0;i<f.width*f.height;++i)assert(s.pixels[f.buffer][i]==(0xff000000u|(uint32_t)f.id));
            ++observations;irq=chos_spin_lock_irqsave(&lock);assert(gui_surface_release(&s,&f));chos_spin_unlock_irqrestore(&lock,irq);
        }else sched_yield();
        assert(now()<deadline);
    }while(!__atomic_load_n(&writer_done,__ATOMIC_ACQUIRE));return NULL;
}
int main(void) {
    chos_gui_surface_begin_t b={.width=129,.height=2};gui_surface_frame_t a,c;
    assert(!gui_surface_acquire(&s,&a));assert(gui_surface_begin(&s,0,&b)==-CHOS_ERRNO_EINVAL);
    assert(!gui_surface_begin(&s,++nonce,&b));uint64_t id=nonce;
    assert(gui_surface_present(&s,id)==-CHOS_ERRNO_EAGAIN);
    chos_gui_surface_tile_t t={.pixel_count=128};
    for(unsigned i=0;i<128;++i)t.pixels[i]=0x123456;
    assert(!gui_surface_tile(&s,id,&t));assert(!gui_surface_tile(&s,id,&t));assert(s.covered==1);
    t.tile_index=3;t.pixel_count=128;assert(gui_surface_tile(&s,id,&t)==-CHOS_ERRNO_EINVAL);
    assert(s.covered==1);assert(gui_surface_present(&s,id)==-CHOS_ERRNO_EAGAIN);
    fill(id,129,2,0x123456);assert(!gui_surface_present(&s,id));assert(gui_surface_acquire(&s,&a));
    assert(a.width==129&&a.height==2);for(unsigned i=0;i<258;++i)assert(s.pixels[a.buffer][i]==0xff123456);
    assert(!gui_surface_begin(&s,++nonce,&b));fill(nonce,129,2,0x654321);assert(!gui_surface_present(&s,nonce));
    assert(gui_surface_acquire(&s,&c));assert(gui_surface_begin(&s,nonce+1,&b)==-CHOS_ERRNO_EAGAIN);
    gui_surface_drop(&s);assert(!gui_surface_acquire(&s,&c));
    assert(gui_surface_begin(&s,nonce+1,&b)==-CHOS_ERRNO_EAGAIN);assert(gui_surface_release(&s,&a));
    assert(!gui_surface_begin(&s,++nonce,&b));fill(nonce,129,2,0xabcdef);assert(!gui_surface_present(&s,nonce));
    assert(gui_surface_release(&s,&c));assert(!gui_surface_release(&s,&c));
    assert(gui_surface_tile(&s,id,&t)==-CHOS_ERRNO_EINVAL);
    b.width=1025;assert(gui_surface_begin(&s,nonce+1,&b)==-CHOS_ERRNO_EINVAL);
    b.width=1;b.x=INT32_MAX;assert(gui_surface_begin(&s,nonce+1,&b)==-CHOS_ERRNO_EINVAL);
    b.x=0;b.width=1024;b.height=768;assert(!gui_surface_begin(&s,++nonce,&b));
    fill(nonce,1024,768,0xabcdef);assert(s.covered==6144);assert(!gui_surface_present(&s,nonce));
    assert(gui_surface_acquire(&s,&a));for(unsigned i=0;i<GUI_SURFACE_PIXELS;++i)assert(s.pixels[a.buffer][i]==0xffabcdef);assert(gui_surface_release(&s,&a));
    gui_surface_drop(&s);assert(!s.readers[0]&&!s.readers[1]);
    pthread_t w,r;assert(!pthread_create(&r,NULL,reader,NULL));assert(!pthread_create(&w,NULL,writer,NULL));
    assert(!pthread_join(w,NULL));assert(!pthread_join(r,NULL));assert(observations&&backpressure);
    assert(!s.readers[0]&&!s.readers[1]);gui_surface_drop(&s);
    puts("pixel store: complete coverage,duplicates,tails,max image,stale IDs,pins across owner reset and concurrent retired-frame immutability passed");
}
