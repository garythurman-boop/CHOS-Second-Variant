#include "chos/user.h"
/* Real EL0 client of the optional software-pixel service. No framebuffer access. */
static uint32_t window_id;
static volatile unsigned frames_submitted;
static void begin(chos_gui_surface_message_t *m,unsigned type,size_t payload,uint64_t id) {
    chos_memset(m,0,sizeof(*m));m->header.version=CHOS_GUI_ABI_VERSION;m->header.type=(uint16_t)type;
    m->header.size_bytes=(uint32_t)(offsetof(chos_gui_surface_message_t,payload)+payload);m->header.request_id=id;
}
static long send(chos_gui_surface_message_t *m){return chos_gui_surface_submit(m,m->header.size_bytes);}
static int fail(unsigned line){const char *text="[FAIL] native pixel surface\n";chos_write(text,chos_strlen(text));return (int)line;}
#define CHECK(x) do{if(!(x))return fail(__LINE__);}while(0)
static long create(void) {
    chos_gui_message_t m;chos_memset(&m,0,sizeof(m));m.header.version=CHOS_GUI_ABI_VERSION;
    m.header.type=CHOS_GUI_MSG_CREATE_WINDOW;m.header.size_bytes=offsetof(chos_gui_message_t,payload)+sizeof(m.payload.create_window);
    m.payload.create_window.x=100;m.payload.create_window.y=90;m.payload.create_window.width=600;m.payload.create_window.height=420;
    m.payload.create_window.background_argb=0xffe0e0e0;
    return chos_gui_submit(&m,m.header.size_bytes);
}
static void destroy(uint32_t id) {
    chos_gui_message_t m;chos_memset(&m,0,sizeof(m));m.header.version=CHOS_GUI_ABI_VERSION;
    m.header.type=CHOS_GUI_MSG_DESTROY_WINDOW;m.header.size_bytes=offsetof(chos_gui_message_t,payload)+sizeof(m.payload.window);
    m.payload.window.window_id=id;(void)chos_gui_submit(&m,m.header.size_bytes);
}
static int decorate(void) {
    chos_gui_message_t m;chos_memset(&m,0,sizeof(m));m.header.version=CHOS_GUI_ABI_VERSION;m.header.type=CHOS_GUI_MSG_SET_TITLE;
    m.header.size_bytes=offsetof(chos_gui_message_t,payload)+sizeof(m.payload.set_title);
    const char *title="CHOS Native Pixel Surface";m.payload.set_title.window_id=window_id;m.payload.set_title.title_length=(uint16_t)chos_strlen(title);
    chos_memcpy(m.payload.set_title.title,title,chos_strlen(title));CHECK(!chos_gui_submit(&m,m.header.size_bytes));
    chos_memset(&m,0,sizeof(m));m.header.version=CHOS_GUI_ABI_VERSION;m.header.type=CHOS_GUI_MSG_DRAW_TEXT;
    m.header.size_bytes=offsetof(chos_gui_message_t,payload)+sizeof(m.payload.draw_text);
    m.payload.draw_text.window_id=window_id;m.payload.draw_text.x=12;m.payload.draw_text.y=16;m.payload.draw_text.argb=0xff000080;
    const char *text="Owned XRGB pixels; Space changes frame; Esc closes.";
    m.payload.draw_text.text_length=(uint16_t)chos_strlen(text);chos_memcpy(m.payload.draw_text.text,text,chos_strlen(text));
    CHECK(!chos_gui_submit(&m,m.header.size_bytes));return 0;
}
static int guard_test(void) {
    chos_gui_surface_message_t m;begin(&m,CHOS_GUI_MSG_SURFACE_BEGIN,sizeof(m.payload.begin),0);
    m.payload.begin.window_id=window_id;m.payload.begin.width=m.payload.begin.height=1;
    long id=send(&m);CHECK(id>0);
    long page=chos_map_memory(4096);CHECK(page>0);
    chos_gui_surface_message_t *edge=(void *)(uintptr_t)(page+4096-48);
    chos_memset(edge,0,48);edge->header.version=CHOS_GUI_ABI_VERSION;edge->header.type=CHOS_GUI_MSG_SURFACE_TILE;
    edge->header.size_bytes=offsetof(chos_gui_surface_message_t,payload)+sizeof(edge->payload.tile);edge->header.request_id=(uint64_t)id;
    edge->payload.tile.window_id=window_id;edge->payload.tile.pixel_count=1;edge->payload.tile.pixels[0]=0x112233;
    CHECK(chos_gui_surface_submit(edge,edge->header.size_bytes)==-CHOS_ERRNO_EFAULT);
    begin(&m,CHOS_GUI_MSG_SURFACE_PRESENT,sizeof(m.payload.window),(uint64_t)id);m.payload.window.window_id=window_id;
    CHECK(send(&m)==-CHOS_ERRNO_EAGAIN);CHECK(!chos_unmap_memory((void *)(uintptr_t)page,4096));
    CHECK(chos_gui_surface_submit(NULL,sizeof(m))==-CHOS_ERRNO_EFAULT);
    CHECK(chos_gui_surface_submit(NULL,sizeof(m)+1)==-CHOS_ERRNO_EINVAL);
    return 0;
}
static int paint(unsigned epoch) {
    chos_gui_surface_message_t m;begin(&m,CHOS_GUI_MSG_SURFACE_BEGIN,sizeof(m.payload.begin),0);
    m.payload.begin.window_id=window_id;m.payload.begin.x=12;m.payload.begin.y=64;m.payload.begin.width=512;m.payload.begin.height=256;
    long id;unsigned retries=0;
    do{id=send(&m);if(id==-CHOS_ERRNO_EAGAIN){CHECK(++retries<=200);chos_sleep_ticks(1);}}while(id==-CHOS_ERRNO_EAGAIN);
    CHECK(id>0);
    begin(&m,CHOS_GUI_MSG_SURFACE_PRESENT,sizeof(m.payload.window),(uint64_t)id);m.payload.window.window_id=window_id;
    CHECK(send(&m)==-CHOS_ERRNO_EAGAIN);
    begin(&m,CHOS_GUI_MSG_SURFACE_TILE,sizeof(m.payload.tile),(uint64_t)id);
    m.payload.tile.window_id=window_id;m.payload.tile.pixel_count=128;
    for(unsigned row=0;row<256;++row)for(unsigned column=0;column<4;++column){
        m.payload.tile.tile_index=row*4+column;
        for(unsigned i=0;i<128;++i){unsigned x=column*128+i;
            m.payload.tile.pixels[i]=((x+epoch*31u)&255u)<<16 | ((row+epoch*17u)&255u)<<8 | (((x/16u)^(row/16u))&1u?0xe0u:0x20u);
        }
        CHECK(!send(&m));if(!(m.payload.tile.tile_index%32u))chos_yield();
    }
    begin(&m,CHOS_GUI_MSG_SURFACE_PRESENT,sizeof(m.payload.window),(uint64_t)id);m.payload.window.window_id=window_id;
    CHECK(!send(&m));frames_submitted=epoch;
    CHECK(send(&m)==-CHOS_ERRNO_EINVAL); /* A presented frame cannot be replayed. */
    const char *done="[PIXELS] complete frame presented\n";chos_write(done,chos_strlen(done));return 0;
}
int main(int argc,char **argv) {
    chos_gui_surface_message_t m;
    if(argc>1&&!chos_strcmp(argv[1],"denied")){
        CHECK(!((uint64_t)chos_capabilities()&CHOS_PROCESS_CAP(CHOS_SYS_GUI_SUBMIT)));
        begin(&m,CHOS_GUI_MSG_SURFACE_BEGIN,sizeof(m.payload.begin),0);
        CHECK(send(&m)==-CHOS_ERRNO_EPERM);
        const char *ok="[ OK ] native pixel GUI permission refusal passed\n";chos_write(ok,chos_strlen(ok));return 0;
    }
    long w=create();CHECK(w>=100&&w<=103);window_id=(uint32_t)w;
    if(argc>2&&!chos_strcmp(argv[1],"foreign")){
        uint32_t other=0;for(const char *c=argv[2];*c;++c){CHECK(*c>='0'&&*c<='9');CHECK(other<10000);other=other*10u+(unsigned)(*c-'0');}
        begin(&m,CHOS_GUI_MSG_SURFACE_BEGIN,sizeof(m.payload.begin),0);
        m.payload.begin.window_id=other;m.payload.begin.width=m.payload.begin.height=1;CHECK(send(&m)==-CHOS_ERRNO_EPERM);
        destroy(window_id);const char *ok="[ OK ] native pixel foreign-window refusal passed\n";chos_write(ok,chos_strlen(ok));return 0;
    }
    int error=decorate();if(error)return error;error=guard_test();if(error)return error;
    char child[20]="foreign ";unsigned value=window_id,at=8;char digits[10];unsigned count=0;
    do{digits[count++]=(char)('0'+value%10u);value/=10u;}while(value);
    while(count)child[at++]=digits[--count];child[at]=0;
    CHECK(chos_spawn_args("/BIN/PIXELS.CHX",child)>0);
    CHECK(chos_spawn_limited("/BIN/PIXELS.CHX","denied",CHOS_PROCESS_CAP_ALL&~CHOS_PROCESS_CAP(CHOS_SYS_GUI_SUBMIT))>0);
    error=paint(1);if(error)return error;
    const char *ok="[ OK ] native pixel copy guard and complete-frame presentation passed\n";chos_write(ok,chos_strlen(ok));
    for(;;){
        chos_gui_message_t event;long result=chos_gui_poll_event(&event,sizeof(event));
        if(result==-CHOS_ERRNO_EAGAIN){chos_sleep_ticks(1);continue;}
        CHECK(result>=0);
        if(event.header.type==CHOS_GUI_MSG_CLOSE_REQUEST)break;
        if(event.header.type==CHOS_GUI_MSG_KEY_EVENT&&(event.payload.key_event.event_flags&CHOS_GUI_EVENT_KEY_DOWN)){
            if(event.payload.key_event.keycode==1)break;
            if(event.payload.key_event.keycode==57){error=paint(frames_submitted+1);if(error)return error;}
        }
    }
    destroy(window_id);return 0;
}
