#include "config.h"
#include "PNGImageDecoder.h"
#include "JPEGImageDecoder.h"
#include "SharedBuffer.h"
#include "ImageTypes.h"
#include <wtf/Threading.h>
#include <wtf/RunLoop.h>
#include <cairo.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <span>
#include "assets.h"
#ifdef __CHOS__
#include <chos/user.h>
#include <chos/libc.h>
#endif
using namespace WebCore;
using namespace WTF;
#define CHECK(value) do{if(!(value)){std::printf("[FAIL] actual WebCore images line %u\n",__LINE__);std::fflush(stdout);return false;}}while(0)
volatile unsigned frames_submitted;
uint32_t webcore_frame[512*256];
uint64_t webcore_frame_hash;
static uint64_t hash(cairo_surface_t* surface) {
    cairo_surface_flush(surface);unsigned width=cairo_image_surface_get_width(surface),height=cairo_image_surface_get_height(surface);
    int stride=cairo_image_surface_get_stride(surface);const unsigned char* bytes=cairo_image_surface_get_data(surface);
    uint64_t value=UINT64_C(14695981039346656037);
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width*4;++x){value^=bytes[y*stride+x];value*=UINT64_C(1099511628211);}
    return value;
}
static Ref<ScalableImageDecoder> decoder(bool jpeg) {
    if(jpeg)return JPEGImageDecoder::create(AlphaOption::Premultiplied,GammaAndColorProfileOption::Ignored);
    return PNGImageDecoder::create(AlphaOption::Premultiplied,GammaAndColorProfileOption::Ignored);
}
static bool image(bool jpeg,bool fragmented,PlatformImagePtr& result) {
    const unsigned char* first=jpeg?original_start:generated_start;const unsigned char* last=jpeg?original_end:generated_end;
    size_t length=last-first;auto d=decoder(jpeg);unsigned count=0;
    const size_t cuts[]={1,7,8,20,33,length/2,length-1,length};
    if(fragmented){
        for(size_t end:cuts){
            auto input=SharedBuffer::create(std::span<const uint8_t>(first,end));d->setData(input.get(),end==length);
            CHECK(!d->failed());
            if(d->isSizeAvailable()){
                auto partial=d->createFrameImageAtIndex(0);++count;
                if(end==length)result=std::move(partial);
            }
        }
        CHECK(count>1);
    }else{
        auto input=SharedBuffer::create(std::span<const uint8_t>(first,length));d->setData(input.get(),true);
        CHECK(!d->failed());result=d->createFrameImageAtIndex(0);
    }
    CHECK(result&&cairo_surface_status(result.get())==CAIRO_STATUS_SUCCESS);
    CHECK(d->frameCount()==1&&d->frameIsCompleteAtIndex(0));
    CHECK(cairo_image_surface_get_format(result.get())==CAIRO_FORMAT_ARGB32);
    CHECK(cairo_image_surface_get_width(result.get())==int(jpeg?original_width:generated_width));
    CHECK(cairo_image_surface_get_height(result.get())==int(jpeg?original_height:generated_height));
    CHECK(hash(result.get())==(jpeg?original_hash:generated_hash));
    // The returned Cairo surface retains the actual WebCore DataSegment. Its
    // pixels must remain valid after this decoder and its input go out of scope.
    std::printf("[WEBCORE IMAGE] %s %s hash=%016llx\n",jpeg?"JPEG":"PNG",fragmented?"fragmented":"complete",(unsigned long long)hash(result.get()));
    return true;
}
static bool partial_cancel(bool jpeg){
    const unsigned char* first=jpeg?original_start:generated_start;size_t size=(jpeg?original_end-original_start:generated_end-generated_start)/2;
    auto d=decoder(jpeg);auto input=SharedBuffer::create(std::span<const uint8_t>(first,size));
    d->setData(input.get(),false);CHECK(!d->failed());
    if(d->isSizeAvailable())d->createFrameImageAtIndex(0);
    return true;
}
static bool invalid(bool jpeg){
    const uint8_t bytes[]={1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
    auto d=decoder(jpeg);auto input=SharedBuffer::create(std::span<const uint8_t>(bytes));d->setData(input.get(),true);
    auto surface=d->createFrameImageAtIndex(0);CHECK(!surface&&d->failed());return true;
}
static bool large_image(){
    const size_t length=large_end-large_start;
    for(unsigned mode=0;mode<2;++mode){
        PlatformImagePtr held;
        {
            auto d=decoder(false);
            if(mode){
                const size_t cuts[]={1,7,33,length/3,length-1,length};
                for(size_t end:cuts){
                    auto input=SharedBuffer::create(std::span<const uint8_t>(large_start,end));
                    d->setData(input.get(),end==length);CHECK(!d->failed());
                    if(d->isSizeAvailable())held=d->createFrameImageAtIndex(0);
                }
            }else{
                auto input=SharedBuffer::create(std::span<const uint8_t>(large_start,length));
                d->setData(input.get(),true);held=d->createFrameImageAtIndex(0);
            }
            CHECK(!d->failed()&&d->frameIsCompleteAtIndex(0));
            CHECK(held&&cairo_surface_status(held.get())==CAIRO_STATUS_SUCCESS);
            CHECK(cairo_image_surface_get_width(held.get())==int(large_width));
            CHECK(cairo_image_surface_get_height(held.get())==int(large_height));
            CHECK(cairo_image_surface_get_stride(held.get())==int(large_width*4));
            CHECK(hash(held.get())==large_hash);
        }
        CHECK(hash(held.get())==large_hash);
        std::printf("[WEBCORE LARGE] mode=%u bytes=%u hash=%016llx\n",mode,
                    large_width*large_height*4,(unsigned long long)hash(held.get()));
    }
    puts("[ OK ] actual WebCore large contiguous image passed");return true;
}
static bool compose(const PlatformImagePtr& png,const PlatformImagePtr& jpeg,unsigned epoch=1){
    cairo_surface_t* output=cairo_image_surface_create_for_data(reinterpret_cast<unsigned char*>(webcore_frame),CAIRO_FORMAT_ARGB32,512,256,512*4);
    CHECK(cairo_surface_status(output)==CAIRO_STATUS_SUCCESS);cairo_t* c=cairo_create(output);
    cairo_set_source_rgb(c,224.0/255,230.0/255,(236.0-(epoch-1)*4)/255);cairo_paint(c);
    cairo_save(c);cairo_translate(c,16+(epoch-1)*8,32);cairo_scale(c,2,2);cairo_set_source_surface(c,png.get(),0,0);
    cairo_pattern_set_filter(cairo_get_source(c),CAIRO_FILTER_NEAREST);cairo_rectangle(c,0,0,generated_width,generated_height);cairo_fill(c);cairo_restore(c);
    cairo_set_source_surface(c,jpeg.get(),270-(epoch-1)*8,32);cairo_rectangle(c,270-(epoch-1)*8,32,original_width,original_height);cairo_fill(c);
    CHECK(cairo_status(c)==CAIRO_STATUS_SUCCESS);cairo_surface_flush(output);webcore_frame_hash=hash(output);
    cairo_destroy(c);cairo_surface_destroy(output);return true;
}
static bool run(){
    PlatformImagePtr png,jpeg;
    for(unsigned i=0;i<3;++i){
        CHECK(image(false,i%2,png)&&image(true,i%2,jpeg));
        CHECK(hash(png.get())==generated_hash&&hash(jpeg.get())==original_hash);
        CHECK(partial_cancel(false)&&partial_cancel(true));
        CHECK(hash(png.get())==generated_hash&&hash(jpeg.get())==original_hash);
    }
    CHECK(invalid(false)&&invalid(true));CHECK(compose(png,jpeg));
    puts("[ OK ] actual WebCore PNG/JPEG pixels,Cairo ownership,fragmentation and cancellation passed");
    return true;
}
static uint32_t window_id;
static void message(chos_gui_surface_message_t* m,unsigned type,size_t payload,uint64_t request){
    std::memset(m,0,sizeof(*m));m->header.version=CHOS_GUI_ABI_VERSION;m->header.type=type;
    m->header.size_bytes=offsetof(chos_gui_surface_message_t,payload)+payload;m->header.request_id=request;
}
static bool present(unsigned epoch){
    PlatformImagePtr png,jpeg;
    CHECK(image(false,epoch%2,png)&&image(true,epoch%2,jpeg));CHECK(compose(png,jpeg,epoch));
    chos_gui_surface_message_t m;message(&m,CHOS_GUI_MSG_SURFACE_BEGIN,sizeof(m.payload.begin),0);
    m.payload.begin.window_id=window_id;m.payload.begin.x=12;m.payload.begin.y=64;m.payload.begin.width=512;m.payload.begin.height=256;
    long id;unsigned retries=0;do{id=chos_gui_surface_submit(&m,m.header.size_bytes);if(id==-CHOS_ERRNO_EAGAIN){CHECK(++retries<200);chos_sleep_ticks(1);}}while(id==-CHOS_ERRNO_EAGAIN);CHECK(id>0);
    message(&m,CHOS_GUI_MSG_SURFACE_TILE,sizeof(m.payload.tile),id);m.payload.tile.window_id=window_id;m.payload.tile.pixel_count=128;
    for(unsigned tile=0;tile<1024;++tile){m.payload.tile.tile_index=tile;std::memcpy(m.payload.tile.pixels,webcore_frame+tile*128,512);CHECK(!chos_gui_surface_submit(&m,m.header.size_bytes));if(!(tile%32))chos_yield();}
    message(&m,CHOS_GUI_MSG_SURFACE_PRESENT,sizeof(m.payload.window),id);m.payload.window.window_id=window_id;CHECK(!chos_gui_surface_submit(&m,m.header.size_bytes));frames_submitted=epoch;
    std::printf("[WEBCORE FRAME] epoch=%u hash=%016llx\n",epoch,(unsigned long long)webcore_frame_hash);return true;
}
static bool window(){
    chos_gui_message_t m={};m.header.version=CHOS_GUI_ABI_VERSION;m.header.type=CHOS_GUI_MSG_CREATE_WINDOW;
    m.header.size_bytes=offsetof(chos_gui_message_t,payload)+sizeof(m.payload.create_window);
    m.payload.create_window.x=100;m.payload.create_window.y=90;m.payload.create_window.width=600;m.payload.create_window.height=420;m.payload.create_window.background_argb=0xffe0e0e0;
    long id=chos_gui_submit(&m,m.header.size_bytes);CHECK(id>=100&&id<=103);window_id=id;
    std::memset(&m,0,sizeof(m));m.header.version=CHOS_GUI_ABI_VERSION;m.header.type=CHOS_GUI_MSG_SET_TITLE;
    m.header.size_bytes=offsetof(chos_gui_message_t,payload)+sizeof(m.payload.set_title);m.payload.set_title.window_id=window_id;
    const char* title="CHOS WebCore Images";m.payload.set_title.title_length=std::strlen(title);std::memcpy(m.payload.set_title.title,title,std::strlen(title));CHECK(!chos_gui_submit(&m,m.header.size_bytes));return true;
}
static bool gui(){
    CHECK(window()&&present(1));
    for(;;){chos_gui_message_t event;long r=chos_gui_poll_event(&event,sizeof(event));if(r==-CHOS_ERRNO_EAGAIN){chos_sleep_ticks(1);continue;}CHECK(r>=0);
        if(event.header.type==CHOS_GUI_MSG_CLOSE_REQUEST)break;
        if(event.header.type==CHOS_GUI_MSG_KEY_EVENT&&(event.payload.key_event.event_flags&CHOS_GUI_EVENT_KEY_DOWN)){
            if(event.payload.key_event.keycode==1)break;if(event.payload.key_event.keycode==57&&frames_submitted<3)CHECK(present(frames_submitted+1));}}
    chos_gui_message_t close={};close.header.version=CHOS_GUI_ABI_VERSION;close.header.type=CHOS_GUI_MSG_DESTROY_WINDOW;
    close.header.size_bytes=offsetof(chos_gui_message_t,payload)+sizeof(close.payload.window);close.payload.window.window_id=window_id;CHECK(!chos_gui_submit(&close,close.header.size_bytes));return true;
}
int main(int argc,char** argv){
    WTF::initialize();if(!run())return 1;
    if(argc==2&&!std::strcmp(argv[1],"large")&&!large_image())return 1;
    cairo_debug_reset_static_data();
    puts("[ OK ] actual WebCore image decoding passed");
    chos_heap_stats_t warm;if(chos_libc_heap_info(&warm))return 1;
    if(!(argc==2&&!std::strcmp(argv[1],"cli"))&&!gui())return 1;
    cairo_debug_reset_static_data();
    chos_heap_stats_t stats;if(chos_libc_heap_info(&stats))return 1;
    std::printf("[WEBCORE HEAP] allocations=%lu mapped=%lu warm_allocations=%lu warm_mapped=%lu\n",(unsigned long)stats.allocations,(unsigned long)stats.mapped_bytes,(unsigned long)warm.allocations,(unsigned long)warm.mapped_bytes);
    if(stats.allocations!=warm.allocations||stats.mapped_bytes!=warm.mapped_bytes){puts("[FAIL] WebCore process-global warm allocation baseline changed");return 1;}
    puts("[ OK ] actual WebCore image application completed and owned surfaces released");puts("[ OK ] actual WebCore image application closed");return 0;
}
