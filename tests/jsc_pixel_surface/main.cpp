#include <JavaScriptCore/JavaScript.h>
#include <JavaScriptCore/JSContextRefPrivate.h>
#include <chos/user.h>
#include <chos/gui_surface.h>
#include <cstdio>

// An actual engine client of the owned-copy GUI API. This exercises typed-array
// backing lifetime; it is not WebCore, HTML/CSS, Canvas or a browser substitute.
extern "C" { volatile unsigned frames_submitted; }
static JSGlobalContextRef context;
static JSValueRef protectedPixels;
static uint32_t windowId;
static long submit(chos_gui_surface_message_t& m) {
    return chos_syscall2(CHOS_SYS_GUI_SUBMIT,(long)&m,m.header.size_bytes);
}
static void begin(chos_gui_surface_message_t& m,unsigned type,size_t bytes,uint64_t id=0) {
    chos_memset(&m,0,sizeof(m));m.header.version=CHOS_GUI_ABI_VERSION;m.header.type=type;
    m.header.size_bytes=offsetof(chos_gui_surface_message_t,payload)+bytes;m.header.request_id=id;
}
static JSValueRef evaluate(const char* script) {
    JSValueRef exception=nullptr;auto string=JSStringCreateWithUTF8CString(script);
    auto result=JSEvaluateScript(context,string,nullptr,nullptr,1,&exception);JSStringRelease(string);
    if(exception||!result){puts("[FAIL] JavaScript pixel evaluation");return nullptr;}
    return result;
}
static bool paint(unsigned epoch) {
    char script[64];std::snprintf(script,sizeof(script),"paintPixels(%u)",epoch);
    auto group=JSContextGetGroup(context);
    JSContextGroupSetExecutionTimeLimit(group,5.0,nullptr,nullptr);
    auto value=evaluate(script);
    JSContextGroupClearExecutionTimeLimit(group);
    if(!value)return false;
    JSValueProtect(context,value);protectedPixels=value;
    JSValueRef exception=nullptr;
    if(JSValueGetTypedArrayType(context,value,&exception)!=kJSTypedArrayTypeUint32Array||exception)return false;
    auto object=JSValueToObject(context,value,&exception);if(!object||exception)return false;
    if(JSObjectGetTypedArrayLength(context,object,&exception)!=512u*256u||exception)return false;
    if(JSObjectGetTypedArrayByteLength(context,object,&exception)!=512u*256u*4u||exception)return false;
    // No engine API call until copying has finished: this temporary pointer may
    // be invalidated by a subsequent JSC API call. User pages are never shared.
    auto pixels=static_cast<const uint32_t*>(JSObjectGetTypedArrayBytesPtr(context,object,&exception));
    if(!pixels||exception)return false;
    chos_gui_surface_message_t m;begin(m,CHOS_GUI_MSG_SURFACE_BEGIN,sizeof(m.payload.begin));
    m.payload.begin.window_id=windowId;m.payload.begin.x=12;m.payload.begin.y=64;
    m.payload.begin.width=512;m.payload.begin.height=256;
    long frame;unsigned retries=0;
    do {frame=submit(m);if(frame==-CHOS_ERRNO_EAGAIN){if(++retries>200)return false;chos_sleep_ticks(1);}}while(frame==-CHOS_ERRNO_EAGAIN);
    if(frame<=0)return false;
    begin(m,CHOS_GUI_MSG_SURFACE_TILE,sizeof(m.payload.tile),frame);
    m.payload.tile.window_id=windowId;m.payload.tile.pixel_count=128;
    for(unsigned tile=0;tile<1024;++tile){
        m.payload.tile.tile_index=tile;
        for(unsigned i=0;i<128;++i)m.payload.tile.pixels[i]=pixels[tile*128+i];
        if(submit(m))return false;
        if(!(tile%32))chos_yield();
    }
    JSValueUnprotect(context,protectedPixels);protectedPixels=nullptr;
    begin(m,CHOS_GUI_MSG_SURFACE_PRESENT,sizeof(m.payload.window),frame);m.payload.window.window_id=windowId;
    if(submit(m))return false;
    frames_submitted=epoch;JSGarbageCollect(context);
    puts("[PIXELS] actual JavaScript Uint32Array frame presented");return true;
}
static bool create() {
    chos_gui_message_t m{};m.header.version=CHOS_GUI_ABI_VERSION;m.header.type=CHOS_GUI_MSG_CREATE_WINDOW;
    m.header.size_bytes=offsetof(chos_gui_message_t,payload)+sizeof(m.payload.create_window);
    m.payload.create_window.x=100;m.payload.create_window.y=90;m.payload.create_window.width=600;m.payload.create_window.height=420;
    m.payload.create_window.background_argb=0xffe0e0e0;
    long id=chos_gui_submit(&m,m.header.size_bytes);if(id<100||id>103)return false;windowId=id;
    m={};m.header.version=CHOS_GUI_ABI_VERSION;m.header.type=CHOS_GUI_MSG_SET_TITLE;
    m.header.size_bytes=offsetof(chos_gui_message_t,payload)+sizeof(m.payload.set_title);
    const char* title="CHOS JavaScript Pixel Surface";m.payload.set_title.window_id=windowId;
    m.payload.set_title.title_length=chos_strlen(title);chos_memcpy(m.payload.set_title.title,title,chos_strlen(title));
    return chos_gui_submit(&m,m.header.size_bytes)==0;
}
int main() {
    puts("[INFO] actual JavaScript pixel VM begins");context=JSGlobalContextCreate(nullptr);
    if(!context)return 2;
    // Every color is computed by the original native JSC interpreter. There is
    // deliberately no native fallback pixel generator when evaluation fails.
    auto setup=evaluate(R"JS(
        globalThis.pixelData=new Uint32Array(512*256);
        globalThis.paintPixels=(epoch)=>{
            for(let y=0;y<256;++y)for(let x=0;x<512;++x)
                pixelData[y*512+x]=(((x+epoch*31)&255)<<16)|(((y+epoch*17)&255)<<8)|((((x>>4)^(y>>4))&1)?224:32);
            return pixelData;
        };true;
    )JS");
    bool ok=setup&&JSValueToBoolean(context,setup)&&create()&&paint(1);
    if(ok)puts("[ OK ] actual JavaScript typed-array pixel presentation passed");
    while(ok){
        chos_gui_message_t event;long r=chos_gui_poll_event(&event,sizeof(event));
        if(r==-CHOS_ERRNO_EAGAIN){chos_sleep_ticks(1);continue;}
        if(r<0){ok=false;break;}
        if(event.header.type==CHOS_GUI_MSG_CLOSE_REQUEST)break;
        if(event.header.type==CHOS_GUI_MSG_KEY_EVENT&&(event.payload.key_event.event_flags&CHOS_GUI_EVENT_KEY_DOWN)){
            if(event.payload.key_event.keycode==1)break;
            if(event.payload.key_event.keycode==57)ok=paint(frames_submitted+1);
        }
    }
    if(windowId){chos_gui_message_t m{};m.header.version=CHOS_GUI_ABI_VERSION;m.header.type=CHOS_GUI_MSG_DESTROY_WINDOW;
        m.header.size_bytes=offsetof(chos_gui_message_t,payload)+sizeof(m.payload.window);m.payload.window.window_id=windowId;
        if(chos_gui_submit(&m,m.header.size_bytes))ok=false;}
    if(protectedPixels)JSValueUnprotect(context,protectedPixels);
    JSGarbageCollect(context);JSGlobalContextRelease(context);
    puts(ok?"[ OK ] actual JavaScript pixel VM cleaned up":"[FAIL] actual JavaScript pixel VM");return ok?0:3;
}
