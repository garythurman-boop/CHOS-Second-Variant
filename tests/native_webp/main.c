#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <webp/decode.h>
#include <webp/demux.h>
#ifdef __CHOS__
#include <chos/libc.h>
#endif
#include "webp-cases.h"
#define CHECK(x) do { if (!(x)) { printf("[FAIL] native WebP line %u\n",__LINE__);fflush(stdout);return 1; } } while(0)
static size_t complete_pixels,stream_deliveries,partial_demuxes,allocation_trials;
typedef struct { void *pointer; size_t size; } Block;
typedef struct { int active,injected,overflow;size_t attempts,fail_at,live,peak;Block blocks[512]; } Ledger;
/* Allocation-failure sweeps are serial. Do not use lazy emulated TLS in a
 * malloc interposer: bootstrapping that TLS legitimately allocates memory. The
 * ledger is disabled and empty before concurrent workers begin. */
static Ledger ledger;
extern void *__real_malloc(size_t);
extern void *__real_calloc(size_t,size_t);
extern void *__real_realloc(void *,size_t);
extern void __real_free(void *);
static int reject_allocation(void){
    if(!ledger.active)return 0;
    ++ledger.attempts;
    if(ledger.fail_at==ledger.attempts){ledger.injected=1;return 1;}
    return 0;
}
static void remember(void *pointer,size_t size){
    if(!pointer||!ledger.active)return;
    unsigned i=0;while(i<512&&ledger.blocks[i].pointer)++i;
    if(i==512){ledger.overflow=1;abort();}
    ledger.blocks[i]=(Block){pointer,size};ledger.live+=size;
    if(ledger.live>ledger.peak)ledger.peak=ledger.live;
}
static void forget(void *pointer){
    if(!pointer)return;
    for(unsigned i=0;i<512;++i)if(ledger.blocks[i].pointer==pointer){
        ledger.live-=ledger.blocks[i].size;ledger.blocks[i]=(Block){0};return;
    }
}
void *__wrap_malloc(size_t size){if(reject_allocation())return NULL;void *p=__real_malloc(size);remember(p,size);return p;}
void *__wrap_calloc(size_t count,size_t size){
    if(reject_allocation()||(size&&count>SIZE_MAX/size))return NULL;
    void *p=__real_calloc(count,size);remember(p,count*size);return p;
}
void *__wrap_realloc(void *old,size_t size){
    if(!ledger.active)return __real_realloc(old,size);
    if(reject_allocation())return NULL;
    unsigned slot=0;while(slot<512&&ledger.blocks[slot].pointer!=old)++slot;
    void *p=__real_realloc(old,size);
    if(p||!size){
        if(slot<512){ledger.live-=ledger.blocks[slot].size;ledger.blocks[slot]=(Block){0};}
        remember(p,size);
    }return p;
}
void __wrap_free(void *pointer){if(ledger.active)forget(pointer);__real_free(pointer);}
static int pixels(const struct WebPCase *spec,const uint8_t *rgba,size_t stride){
    for(unsigned y=0;y<spec->height;++y)if(memcmp(rgba+y*stride,spec->rgba+y*spec->width*4,spec->width*4))return 1;
    return 0;
}
static int complete(const struct WebPCase *spec){
    WebPBitstreamFeatures features;CHECK(WebPGetFeatures(spec->data,spec->length,&features)==VP8_STATUS_OK);
    CHECK(features.width==(int)spec->width&&features.height==(int)spec->height&&features.has_alpha);
    CHECK(features.format==(!strcmp(spec->name,"lossy")?1:2));
    int width=0,height=0;uint8_t *rgba=WebPDecodeRGBA(spec->data,spec->length,&width,&height);
    CHECK(rgba&&width==(int)spec->width&&height==(int)spec->height&&!pixels(spec,rgba,spec->width*4));
    WebPFree(rgba);
    size_t stride=spec->width*4+12,capacity=stride*spec->height;
    rgba=malloc(capacity+16);CHECK(rgba);memset(rgba,0xa5,capacity+16);
    CHECK(WebPDecodeRGBAInto(spec->data,spec->length,rgba,capacity,(int)stride)==rgba&&!pixels(spec,rgba,stride));
    for(unsigned y=0;y<spec->height;++y)for(size_t x=spec->width*4;x<stride;++x)CHECK(rgba[y*stride+x]==0xa5);
    for(size_t x=capacity;x<capacity+16;++x)CHECK(rgba[x]==0xa5);
    CHECK(!WebPDecodeRGBAInto(spec->data,spec->length,rgba,spec->width*spec->height*4-1,(int)(spec->width*4)));
    free(rgba);complete_pixels+=spec->width*spec->height*2;return 0;
}
static int incremental(const struct WebPCase *spec,size_t fragment,int update,int cancel){
    size_t stride=spec->width*4+8,capacity=stride*spec->height;
    uint8_t *output=malloc(capacity+16);CHECK(output);memset(output,0xa5,capacity+16);
    WebPIDecoder *decoder=WebPINewRGB(MODE_RGBA,output,capacity,(int)stride);CHECK(decoder);
    size_t position=0;VP8StatusCode status=VP8_STATUS_SUSPENDED;
    size_t limit=cancel?spec->length/2:spec->length;
    while(position<limit){size_t amount=limit-position;if(amount>fragment)amount=fragment;
        status=update?WebPIUpdate(decoder,spec->data,position+amount):WebPIAppend(decoder,spec->data+position,amount);
        CHECK(status==VP8_STATUS_OK||status==VP8_STATUS_SUSPENDED);
        position+=amount;++stream_deliveries;
        int rows=0,width=0,height=0,decoded_stride=0;
        uint8_t *decoded=WebPIDecGetRGB(decoder,&rows,&width,&height,&decoded_stride);
        if(decoded){
            CHECK(decoded==output&&rows>=0&&rows<=(int)spec->height&&decoded_stride==(int)stride);
            /* The API can expose caller-owned storage before the image
             * header is complete; dimensions are meaningful after rows exist. */
            if(rows)CHECK(width==(int)spec->width&&height==(int)spec->height);
        }
    }
    if(!cancel){CHECK(status==VP8_STATUS_OK&&!pixels(spec,output,stride));
        int rows=0;CHECK(WebPIDecGetRGB(decoder,&rows,NULL,NULL,NULL)==output&&rows==(int)spec->height);
    }
    WebPIDelete(decoder);
    for(unsigned y=0;y<spec->height;++y)for(size_t x=spec->width*4;x<stride;++x)CHECK(output[y*stride+x]==0xa5);
    for(size_t x=capacity;x<capacity+16;++x)CHECK(output[x]==0xa5);
    free(output);return 0;
}
static int demux_static(const struct WebPCase *spec){
    WebPData data={spec->data,spec->length};WebPDemuxer *demux=WebPDemux(&data);CHECK(demux);
    CHECK(WebPDemuxGetI(demux,WEBP_FF_CANVAS_WIDTH)==spec->width&&WebPDemuxGetI(demux,WEBP_FF_CANVAS_HEIGHT)==spec->height);
    CHECK(WebPDemuxGetI(demux,WEBP_FF_FRAME_COUNT)==1);
    WebPIterator it;CHECK(WebPDemuxGetFrame(demux,1,&it));
    CHECK(it.complete&&it.width==(int)spec->width&&it.height==(int)spec->height);
    int w=0,h=0;uint8_t *rgba=WebPDecodeRGBA(it.fragment.bytes,it.fragment.size,&w,&h);
    CHECK(rgba&&w==(int)spec->width&&h==(int)spec->height&&!pixels(spec,rgba,spec->width*4));
    WebPFree(rgba);WebPDemuxReleaseIterator(&it);WebPDemuxDelete(demux);
    for(size_t size=1;size<=spec->length;){
        data.size=size;WebPDemuxState state;demux=WebPDemuxPartial(&data,&state);++partial_demuxes;
        CHECK(state!=WEBP_DEMUX_PARSE_ERROR);
        if(size==spec->length)CHECK(demux&&state==WEBP_DEMUX_DONE);
        if(demux)WebPDemuxDelete(demux);
        if(size==spec->length)break;size+=7;if(size>spec->length)size=spec->length;
    }
    return 0;
}
static int animation(void){
    WebPData data={animation_webp,sizeof(animation_webp)};
    WebPDemuxer *demux=WebPDemux(&data);CHECK(demux);
    CHECK(WebPDemuxGetI(demux,WEBP_FF_FRAME_COUNT)==2&&WebPDemuxGetI(demux,WEBP_FF_LOOP_COUNT)==3);
    CHECK(WebPDemuxGetI(demux,WEBP_FF_FORMAT_FLAGS)&ANIMATION_FLAG);
    WebPIterator it;CHECK(WebPDemuxGetFrame(demux,1,&it)&&it.duration==80&&it.complete);
    CHECK(WebPDemuxNextFrame(&it)&&it.duration==120&&it.complete&&!WebPDemuxNextFrame(&it));
    WebPDemuxReleaseIterator(&it);WebPDemuxDelete(demux);
    WebPAnimDecoderOptions options;CHECK(WebPAnimDecoderOptionsInit(&options));
    options.color_mode=MODE_RGBA;options.use_threads=1;
    WebPAnimDecoder *decoder=WebPAnimDecoderNew(&data,&options);CHECK(decoder);
    WebPAnimInfo info;CHECK(WebPAnimDecoderGetInfo(decoder,&info)&&info.canvas_width==19&&info.canvas_height==13&&info.frame_count==2&&info.loop_count==3);
    for(unsigned iteration=0;iteration<2;++iteration){
        uint8_t *rgba=NULL;int timestamp=0;
        CHECK(WebPAnimDecoderHasMoreFrames(decoder)&&WebPAnimDecoderGetNext(decoder,&rgba,&timestamp)&&timestamp==80);
        CHECK(!memcmp(rgba,lossless_rgba,sizeof(lossless_rgba)));
        CHECK(WebPAnimDecoderHasMoreFrames(decoder)&&WebPAnimDecoderGetNext(decoder,&rgba,&timestamp)&&timestamp==200);
        CHECK(!memcmp(rgba,frame2_rgba,sizeof(frame2_rgba))&&!WebPAnimDecoderHasMoreFrames(decoder));
        WebPAnimDecoderReset(decoder);
    }
    WebPAnimDecoderDelete(decoder);return 0;
}
static int rejection(void){
    const struct WebPCase *spec=&webp_cases[0];int w=0,h=0;
    const size_t cuts[]={0,1,11,20,sizeof(lossless_webp)/2};
    for(unsigned i=0;i<sizeof(cuts)/sizeof(cuts[0]);++i)CHECK(!WebPDecodeRGBA(spec->data,cuts[i],&w,&h));
    uint8_t *bad=malloc(spec->length);CHECK(bad);memcpy(bad,spec->data,spec->length);bad[0]='X';
    CHECK(!WebPDecodeRGBA(bad,spec->length,&w,&h));
    WebPData data={bad,spec->length};CHECK(!WebPDemux(&data));
    WebPDemuxState state;CHECK(!WebPDemuxPartial(&data,&state)&&state==WEBP_DEMUX_PARSE_ERROR);
    memcpy(bad,spec->data,spec->length);memset(bad+4,0xff,4);CHECK(!WebPDemux(&data));
    free(bad);return 0;
}
static int allocation_operation(void){
    int successes=0,w=0,h=0;
    uint8_t *rgba=WebPDecodeRGBA(lossless_webp,sizeof(lossless_webp),&w,&h);
    if(rgba){if(w!=19||h!=13||memcmp(rgba,lossless_rgba,sizeof(lossless_rgba)))abort();++successes;WebPFree(rgba);}
    WebPData data={animation_webp,sizeof(animation_webp)};WebPDemuxer *demux=WebPDemux(&data);
    if(demux){if(WebPDemuxGetI(demux,WEBP_FF_FRAME_COUNT)!=2)abort();++successes;WebPDemuxDelete(demux);}
    return successes;
}
static int allocation_failures(void){
    memset(&ledger,0,sizeof(ledger));ledger.active=1;int successes=allocation_operation();ledger.active=0;
    CHECK(successes==2&&!ledger.live&&!ledger.overflow&&ledger.attempts);
    size_t attempts=ledger.attempts;
    for(size_t fail=1;fail<=attempts;++fail){
        memset(&ledger,0,sizeof(ledger));ledger.active=1;ledger.fail_at=fail;allocation_operation();ledger.active=0;
        CHECK(ledger.injected&&!ledger.live&&!ledger.overflow);++allocation_trials;
    }
    memset(&ledger,0,sizeof(ledger));CHECK(allocation_operation()==2);return 0;
}
static void *worker(void *argument){
    const struct WebPCase *spec=&webp_cases[(uintptr_t)argument%3];
    for(unsigned i=0;i<12;++i){int w=0,h=0;uint8_t *rgba=WebPDecodeRGBA(spec->data,spec->length,&w,&h);
        if(!rgba||w!=19||h!=13||memcmp(rgba,spec->rgba,19*13*4)){WebPFree(rgba);return (void*)1;}
        WebPFree(rgba);
    }
    return NULL;
}
static int threads(void){
    pthread_t workers[3];pthread_attr_t attributes;CHECK(!pthread_attr_init(&attributes)&&!pthread_attr_setstacksize(&attributes,128*1024));
    for(unsigned i=0;i<3;++i)CHECK(!pthread_create(&workers[i],&attributes,worker,(void*)(uintptr_t)i));
    for(unsigned i=0;i<3;++i){void *result=NULL;CHECK(!pthread_join(workers[i],&result)&&!result);}
    CHECK(!pthread_attr_destroy(&attributes));return 0;
}
int main(void){
    CHECK(WebPGetDecoderVersion()==WEBP_EXPECTED_VERSION&&WebPGetDemuxVersion()==WEBP_EXPECTED_VERSION);
    for(unsigned i=0;i<sizeof(webp_cases)/sizeof(webp_cases[0]);++i){
        const struct WebPCase *spec=&webp_cases[i];CHECK(!complete(spec)&&!demux_static(spec));
        CHECK(!incremental(spec,1,0,0)&&!incremental(spec,7,0,0)&&!incremental(spec,11,1,0));
        CHECK(!incremental(spec,3,0,1));
    }
    CHECK(!animation()&&!rejection()&&!allocation_failures()&&!threads());
#ifdef __CHOS__
    chos_heap_stats_t stats;CHECK(!chos_libc_heap_info(&stats)&&!stats.allocations&&!stats.mapped_bytes);
#endif
    printf("[ OK ] actual WebP complete pixels=%zu fragmented deliveries=%zu partial demux=%zu allocation failures=%zu\n",complete_pixels,stream_deliveries,partial_demuxes,allocation_trials);
    puts("[ OK ] actual WebP lossless/lossy alpha,animation,cancellation,rejections,pthreads and cleanup passed");return 0;
}
