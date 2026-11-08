#include <chos/text.h>
#include <hb.h>
#include <hb-ot.h>
#include <pthread.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <climits>
#ifndef CHOS_TEXT_HOST
#include <chos/user.h>
#endif
#include "text-cases.h"
#if TEXT_WITH_JSC
extern "C" int chos_text_javascript_start();
extern "C" int chos_text_javascript_frame(unsigned,char [3][256],unsigned *);
extern "C" void chos_text_javascript_stop();
#endif
#ifndef CHOS_TEXT_HOST
extern "C" {
#define FONT_SYMBOL(n) extern const unsigned char chos_font_##n##_start[],chos_font_##n##_end[];
FONT_SYMBOL(0) FONT_SYMBOL(1) FONT_SYMBOL(2) FONT_SYMBOL(3) FONT_SYMBOL(4)
#undef FONT_SYMBOL
}
static const unsigned char *embeddedStart[]={chos_font_0_start,chos_font_1_start,chos_font_2_start,chos_font_3_start,chos_font_4_start};
static const unsigned char *embeddedEnd[]={chos_font_0_end,chos_font_1_end,chos_font_2_end,chos_font_3_end,chos_font_4_end};
#endif
static unsigned char *fontBytes[5];
static size_t fontLengths[5];
static chos_text_font_t *fonts[3];
extern "C" { volatile unsigned frames_submitted;uint64_t text_frame_hash;uint32_t text_frame[512*256]; }
static int failure(int line){std::printf("[FAIL] native text line %d\n",line);return 1;}
#define CHECK(x) do{if(!(x))return failure(__LINE__);}while(0)
static int load(const char *directory,bool filesystem) {
    const char *names[]={"NotoSans-Regular.ttf","NotoNaskhArabic-Regular.ttf","NotoSansDevanagari-Regular.ttf",
                       "3e46c3b84c1370a06594736c7f8acebf810bbb3b.ttf","872d2955d326bd6676a06f66b8238ebbaabc212f.ttf"};
    for(unsigned i=0;i<5;++i){
#ifndef CHOS_TEXT_HOST
        if(!filesystem){
            size_t size=static_cast<size_t>(reinterpret_cast<uintptr_t>(embeddedEnd[i])-reinterpret_cast<uintptr_t>(embeddedStart[i]));CHECK(size&&size<=CHOS_TEXT_FONT_BYTES_MAX);
            fontBytes[i]=static_cast<unsigned char*>(std::malloc(size));CHECK(fontBytes[i]);fontLengths[i]=size;
            std::memcpy(fontBytes[i],embeddedStart[i],size);continue;
        }
#else
        (void)filesystem;
#endif
        char path[512];
#ifdef CHOS_TEXT_HOST
        std::snprintf(path,sizeof(path),"%s/%s",directory,names[i]);
#else
        (void)directory;(void)names;std::snprintf(path,sizeof(path),"/FONTS/F%u.TTF",i);
#endif
        auto file=std::fopen(path,"rb");CHECK(file);CHECK(!std::fseek(file,0,SEEK_END));long size=std::ftell(file);
        CHECK(size>0&&(unsigned long)size<=CHOS_TEXT_FONT_BYTES_MAX);CHECK(!std::fseek(file,0,SEEK_SET));
        fontBytes[i]=static_cast<unsigned char*>(std::malloc(size));CHECK(fontBytes[i]);fontLengths[i]=size;
        CHECK(std::fread(fontBytes[i],1,size,file)==(size_t)size);CHECK(!std::fclose(file));
    }
    std::puts(filesystem?"[INFO] actual snapshot filesystem fonts loaded":"[INFO] diagnostic embedded original font assets loaded");
    return 0;
}
static int conformance() {
    hb_font_t *referenceFonts[2]{};
    for(unsigned i=3;i<5;++i){
        auto blob=hb_blob_create(reinterpret_cast<char*>(fontBytes[i]),fontLengths[i],HB_MEMORY_MODE_READONLY,nullptr,nullptr);
        auto face=hb_face_create(blob,0);CHECK(hb_face_get_glyph_count(face)>0);referenceFonts[i-3]=hb_font_create(face);
        hb_ot_font_set_funcs(referenceFonts[i-3]);hb_font_set_scale(referenceFonts[i-3],hb_face_get_upem(face),hb_face_get_upem(face));
        hb_face_destroy(face);hb_blob_destroy(blob);
    }
    unsigned passed=0;
    for(auto& item:textCases){
        auto buffer=hb_buffer_create();hb_buffer_add_codepoints(buffer,item.codepoints,item.length,0,item.length);hb_buffer_guess_segment_properties(buffer);
        auto font=referenceFonts[item.font-3];CHECK(hb_shape_full(font,buffer,nullptr,0,nullptr));CHECK(hb_buffer_allocation_successful(buffer));
        char output[8192];unsigned consumed=0;unsigned count=hb_buffer_get_length(buffer);
        CHECK(hb_buffer_serialize_glyphs(buffer,0,count,output,sizeof(output),&consumed,font,HB_BUFFER_SERIALIZE_FORMAT_TEXT,HB_BUFFER_SERIALIZE_FLAG_DEFAULT)==count);
        CHECK(consumed<sizeof(output));output[consumed]=0;
        if(std::strcmp(output,item.expected)){std::printf("[DETAIL] shaper case %u\nactual %s\nexpected %s\n",passed,output,item.expected);hb_buffer_destroy(buffer);return failure(__LINE__);}
        hb_buffer_destroy(buffer);++passed;
    }
    for(auto font:referenceFonts)hb_font_destroy(font);
    CHECK(passed==64);std::puts("[ OK ] 64 original HarfBuzz Arabic normalization vectors passed");return 0;
}
static const char *samples[]={"office affinity AV fi fl","\xd8\xa7\xd9\x84\xd8\xb3\xd9\x84\xd8\xa7\xd9\x85 \xd8\xb9\xd9\x84\xd9\x8a\xd9\x83\xd9\x85",
                            "\xe0\xa4\xa8\xe0\xa4\xae\xe0\xa4\xb8\xe0\xa5\x8d\xe0\xa4\xa4\xe0\xa5\x87 \xe0\xa4\xa6\xe0\xa5\x81\xe0\xa4\xa8\xe0\xa4\xbf\xe0\xa4\xaf\xe0\xa4\xbe"};
static const char *languages[]={"en","ar","hi"};
static int checks() {
    chos_text_font_t *invalid=reinterpret_cast<chos_text_font_t*>(1);
    CHECK(chos_text_font_open(nullptr,0,0,&invalid)==CHOS_TEXT_INVALID&&!invalid);
    const unsigned char broken[]={0,1,2,3,4,5,6,7};
    CHECK(chos_text_font_open(broken,sizeof(broken),0,&invalid)==CHOS_TEXT_BAD_FONT&&!invalid);
    CHECK(chos_text_font_open(fontBytes[0],CHOS_TEXT_FONT_BYTES_MAX+1u,0,&invalid)==CHOS_TEXT_INVALID&&!invalid);
    for(unsigned i=0;i<3;++i)CHECK(!chos_text_font_open(fontBytes[i],fontLengths[i],0,&fonts[i]));
    chos_text_font_t *extra[5]{};
    for(auto& f:extra)CHECK(!chos_text_font_open(fontBytes[0],fontLengths[0],0,&f));
    CHECK(chos_text_font_open(fontBytes[0],fontLengths[0],0,&invalid)==CHOS_TEXT_LIMIT&&!invalid);
    for(auto f:extra)chos_text_font_close(f);
    CHECK(!chos_text_font_open(fontBytes[0],fontLengths[0],0,&invalid));chos_text_font_close(invalid);invalid=nullptr;
    CHECK(!chos_text_font_pixels(fonts[0],24));CHECK(chos_text_font_pixels(fonts[0],0)==CHOS_TEXT_INVALID);CHECK(chos_text_font_pixels(fonts[0],129)==CHOS_TEXT_INVALID);
    uint32_t guarded[2+64*32];for(auto& p:guarded)p=0xffeeeeee;
    chos_text_surface_t surface{guarded+1,60,32,64,64*32};chos_text_metrics_t metrics{};
    CHECK(!chos_text_draw(fonts[0],"A",1,"en",CHOS_TEXT_AUTO,&surface,-8,20,0xff102030,&metrics));
    CHECK(metrics.glyphs==1&&metrics.ink_pixels>0);
    CHECK(guarded[0]==0xffeeeeee&&guarded[64*32+1]==0xffeeeeee);
    for(unsigned row=0;row<32;++row)for(unsigned x=60;x<64;++x)CHECK(guarded[1+row*64+x]==0xffeeeeee);
    uint32_t unchanged=guarded[1];
    CHECK(chos_text_draw(fonts[0],"A",CHOS_TEXT_UTF8_MAX+1,"en",0,&surface,0,0,0,&metrics)==CHOS_TEXT_INVALID);
    surface.capacity_pixels=1;CHECK(chos_text_draw(fonts[0],"A",1,"en",0,&surface,0,0,0,&metrics)==CHOS_TEXT_INVALID);surface.capacity_pixels=64*32;
    CHECK(chos_text_draw(fonts[0],"A",1,"bad hint",0,&surface,0,0,0,&metrics)==CHOS_TEXT_INVALID);
    CHECK(guarded[1]==unchanged);
    // The caller may discard/reuse its input immediately after font creation.
    auto transient=static_cast<unsigned char*>(std::malloc(fontLengths[0]));CHECK(transient);
    std::memcpy(transient,fontBytes[0],fontLengths[0]);chos_text_font_t *owned=nullptr;
    CHECK(!chos_text_font_open(transient,fontLengths[0],0,&owned));std::free(transient);
    auto overwrite=static_cast<unsigned char*>(std::malloc(fontLengths[0]));CHECK(overwrite);std::memset(overwrite,0xa5,fontLengths[0]);
    CHECK(!chos_text_draw(owned,"A",1,"en",0,&surface,2,25,0xff102030,&metrics)&&metrics.ink_pixels>0);
    chos_text_font_close(owned);std::free(overwrite);
    const char malformed[]={char(0xc0),char(0xaf),char(0xff)};
    CHECK(!chos_text_draw(fonts[0],malformed,sizeof(malformed),"en",0,&surface,2,25,0xff000000,&metrics)&&metrics.glyphs==3);
    CHECK(!chos_text_draw(fonts[0],"A",1,"en",0,&surface,INT_MAX,INT_MIN,0,&metrics)&&metrics.ink_pixels==0);
    CHECK(!chos_text_draw(fonts[0],"A",1,"en",0,&surface,INT_MIN,INT_MAX,0,&metrics)&&metrics.ink_pixels==0);
    CHECK(!chos_text_draw(fonts[0],"",0,"en",0,&surface,INT_MAX,INT_MIN,0,&metrics)&&metrics.glyphs==0);
    return 0;
}
struct Worker {unsigned index;int result;};
static void *concurrent(void *input) {
    auto w=static_cast<Worker*>(input);uint32_t pixels[128*64];chos_text_surface_t surface{pixels,128,64,128,128*64};
    for(unsigned iteration=0;iteration<20;++iteration){
        for(auto& p:pixels)p=0xffffffff;chos_text_metrics_t result{};
        if(chos_text_draw(fonts[w->index],samples[w->index],std::strlen(samples[w->index]),languages[w->index],0,&surface,-10,40,0xff000000,&result)||!result.ink_pixels){w->result=1;break;}
    }
    return nullptr;
}
static int concurrency() {
    // Two workers share one actual FreeType/HarfBuzz handle. Another uses an
    // independent script/font. The wrapper's real mutex protects both paths.
    Worker workers[3]={{1,0},{1,0},{2,0}};pthread_t threads[3];
    for(unsigned i=0;i<3;++i)CHECK(!pthread_create(&threads[i],nullptr,concurrent,&workers[i]));
    for(unsigned i=0;i<3;++i)CHECK(!pthread_join(threads[i],nullptr)&&!workers[i].result);
    return 0;
}
static int render(unsigned epoch) {
    unsigned heights[]={24,32,16};unsigned height=heights[(epoch-1)%3];
    const char *lines[3]={samples[0],samples[1],samples[2]};
#if TEXT_WITH_JSC
    char computed[3][256];CHECK(chos_text_javascript_frame(epoch,computed,&height));
    for(unsigned i=0;i<3;++i)lines[i]=computed[i];
#endif
    for(auto& pixel:text_frame)pixel=0xfff5f5f5;
    chos_text_surface_t surface{text_frame,512,256,512,512*256};
    for(unsigned i=0;i<3;++i){
        CHECK(!chos_text_font_pixels(fonts[i],height));chos_text_metrics_t metrics{};
        CHECK(!chos_text_draw(fonts[i],lines[i],std::strlen(lines[i]),languages[i],0,&surface,12,50+i*75,0xff102040,&metrics));
        CHECK(metrics.glyphs>0&&metrics.ink_pixels>0&&metrics.advance_x_26_6>0);
        CHECK(metrics.direction==(i==1?CHOS_TEXT_RTL:CHOS_TEXT_LTR));
        if(i==0)CHECK(metrics.glyphs<std::strlen(lines[i]));
    }
    uint64_t hash=UINT64_C(14695981039346656037);
    for(auto pixel:text_frame)for(unsigned shift=0;shift<32;shift+=8){hash^=(pixel>>shift)&255u;hash*=UINT64_C(1099511628211);}
    text_frame_hash=hash;std::printf("[TEXT] epoch=%u pixels=%u hash=%016llx\n",epoch,height,(unsigned long long)hash);return 0;
}
#ifndef CHOS_TEXT_HOST
static uint32_t windowId;
static void start(chos_gui_surface_message_t& m,unsigned type,size_t payload,uint64_t id=0){
    chos_memset(&m,0,sizeof(m));m.header.version=CHOS_GUI_ABI_VERSION;m.header.type=type;
    m.header.size_bytes=offsetof(chos_gui_surface_message_t,payload)+payload;m.header.request_id=id;
}
static int present(unsigned epoch) {
    int error=render(epoch);if(error)return error;
    chos_gui_surface_message_t m;start(m,CHOS_GUI_MSG_SURFACE_BEGIN,sizeof(m.payload.begin));m.payload.begin.window_id=windowId;
    m.payload.begin.x=12;m.payload.begin.y=64;m.payload.begin.width=512;m.payload.begin.height=256;
    long id;unsigned retries=0;
    do {id=chos_gui_surface_submit(&m,m.header.size_bytes);if(id==-CHOS_ERRNO_EAGAIN){CHECK(++retries<=200);chos_sleep_ticks(1);}}while(id==-CHOS_ERRNO_EAGAIN);
    CHECK(id>0);start(m,CHOS_GUI_MSG_SURFACE_TILE,sizeof(m.payload.tile),id);m.payload.tile.window_id=windowId;m.payload.tile.pixel_count=128;
    for(unsigned tile=0;tile<1024;++tile){m.payload.tile.tile_index=tile;for(unsigned i=0;i<128;++i)m.payload.tile.pixels[i]=text_frame[tile*128+i];CHECK(!chos_gui_surface_submit(&m,m.header.size_bytes));if(!(tile%32))chos_yield();}
    start(m,CHOS_GUI_MSG_SURFACE_PRESENT,sizeof(m.payload.window),id);m.payload.window.window_id=windowId;
    CHECK(!chos_gui_surface_submit(&m,m.header.size_bytes));frames_submitted=epoch;return 0;
}
static int window() {
    chos_gui_message_t m{};m.header.version=CHOS_GUI_ABI_VERSION;m.header.type=CHOS_GUI_MSG_CREATE_WINDOW;
    m.header.size_bytes=offsetof(chos_gui_message_t,payload)+sizeof(m.payload.create_window);
    m.payload.create_window.x=100;m.payload.create_window.y=90;m.payload.create_window.width=600;m.payload.create_window.height=420;m.payload.create_window.background_argb=0xffe0e0e0;
    long id=chos_gui_submit(&m,m.header.size_bytes);CHECK(id>=100&&id<=103);windowId=id;
    m={};m.header.version=CHOS_GUI_ABI_VERSION;m.header.type=CHOS_GUI_MSG_SET_TITLE;m.header.size_bytes=offsetof(chos_gui_message_t,payload)+sizeof(m.payload.set_title);
    const char *title="CHOS Native Shaped Text";m.payload.set_title.window_id=windowId;m.payload.set_title.title_length=std::strlen(title);chos_memcpy(m.payload.set_title.title,title,std::strlen(title));CHECK(!chos_gui_submit(&m,m.header.size_bytes));
    return 0;
}
#endif
int main(int argc,char **argv) {
#ifdef CHOS_TEXT_HOST
    CHECK(argc==3);CHECK(!load(argv[1],true));
#else
    bool filesystem=argc>1&&!std::strcmp(argv[1],"--filesystem");CHECK(!load(nullptr,filesystem));
#endif
    CHECK(!conformance());CHECK(!checks());CHECK(!concurrency());
#if TEXT_WITH_JSC
    CHECK(chos_text_javascript_start());
#endif
    std::puts("[ OK ] native font shaping,rasterization,limits and concurrency passed");
#ifdef CHOS_TEXT_HOST
    for(unsigned epoch=1;epoch<=3;++epoch){CHECK(!render(epoch));char path[512];std::snprintf(path,sizeof(path),"%s/frame%u.rgb",argv[2],epoch);
        auto file=std::fopen(path,"wb");CHECK(file);for(auto pixel:text_frame){unsigned char rgb[3]={(unsigned char)(pixel>>16),(unsigned char)(pixel>>8),(unsigned char)pixel};CHECK(std::fwrite(rgb,1,3,file)==3);}CHECK(!std::fclose(file));}
#else
    CHECK(!window());CHECK(!present(1));
    for(;;){chos_gui_message_t event;long r=chos_gui_poll_event(&event,sizeof(event));if(r==-CHOS_ERRNO_EAGAIN){chos_sleep_ticks(1);continue;}CHECK(r>=0);
        if(event.header.type==CHOS_GUI_MSG_CLOSE_REQUEST)break;
        if(event.header.type==CHOS_GUI_MSG_KEY_EVENT&&(event.payload.key_event.event_flags&CHOS_GUI_EVENT_KEY_DOWN)){
            if(event.payload.key_event.keycode==1)break;
            if(event.payload.key_event.keycode==57)CHECK(!present(frames_submitted+1));}}
    chos_gui_message_t close{};close.header.version=CHOS_GUI_ABI_VERSION;close.header.type=CHOS_GUI_MSG_DESTROY_WINDOW;
    close.header.size_bytes=offsetof(chos_gui_message_t,payload)+sizeof(close.payload.window);close.payload.window.window_id=windowId;CHECK(!chos_gui_submit(&close,close.header.size_bytes));
#endif
    #if TEXT_WITH_JSC
    chos_text_javascript_stop();
#endif
    for(auto font:fonts)chos_text_font_close(font);
    for(auto data:fontBytes)std::free(data);
    std::puts("[ OK ] native text handles and application cleaned up");return 0;
}
