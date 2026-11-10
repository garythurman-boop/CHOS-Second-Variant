#include <cairo.h>
#include <cairo-ft.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#ifndef CHOS_CAIRO_HOST
#include <chos/user.h>
extern const unsigned char cairo_font_start[],cairo_font_end[];
#endif
#define CHECK(x) do{if(!(x)){printf("[FAIL] native Cairo line %u\n",__LINE__);return 1;}}while(0)
static const unsigned char *font_bytes;
static size_t font_size;
volatile unsigned frames_submitted;
uint32_t cairo_frame[512*256];
uint64_t cairo_frame_hash;
static int font_load(const char *path){
#ifdef CHOS_CAIRO_HOST
    FILE *file=fopen(path,"rb");CHECK(file);CHECK(!fseek(file,0,SEEK_END));
    long size=ftell(file);CHECK(size>0&&size<4*1024*1024);CHECK(!fseek(file,0,SEEK_SET));
    unsigned char *p=malloc(size);CHECK(p);CHECK(fread(p,1,size,file)==(size_t)size);CHECK(!fclose(file));font_bytes=p;font_size=size;
#else
    (void)path;font_bytes=cairo_font_start;font_size=(uintptr_t)cairo_font_end-(uintptr_t)cairo_font_start;
#endif
    return 0;
}
static int analytic(void){
    uint32_t storage[2+12*8];for(unsigned i=0;i<sizeof(storage)/4;++i)storage[i]=0xff123456;
    cairo_surface_t *surface=cairo_image_surface_create_for_data((unsigned char*)(storage+1),CAIRO_FORMAT_ARGB32,8,8,12*4);
    CHECK(cairo_surface_status(surface)==CAIRO_STATUS_SUCCESS);cairo_t *c=cairo_create(surface);
    cairo_set_antialias(c,CAIRO_ANTIALIAS_NONE);cairo_set_operator(c,CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgb(c,0,1,0);cairo_paint(c);cairo_rectangle(c,2,2,4,4);cairo_clip(c);
    cairo_set_source_rgb(c,1,0,0);cairo_paint(c);cairo_set_operator(c,CAIRO_OPERATOR_OVER);
    cairo_set_source_rgba(c,0,0,1,0.5);cairo_paint(c);cairo_surface_flush(surface);
    for(unsigned y=0;y<8;++y)for(unsigned x=0;x<12;++x)
        CHECK(storage[1+y*12+x]==(x>=8?0xff123456:x>=2&&x<6&&y>=2&&y<6?0xff7f0080:0xff00ff00));
    CHECK(storage[0]==0xff123456&&storage[97]==0xff123456);
    cairo_destroy(c);cairo_surface_destroy(surface);
    surface=cairo_image_surface_create_for_data((unsigned char*)storage,CAIRO_FORMAT_ARGB32,8,8,1);
    CHECK(cairo_surface_status(surface)==CAIRO_STATUS_INVALID_STRIDE);cairo_surface_destroy(surface);
    surface=cairo_image_surface_create(CAIRO_FORMAT_ARGB32,-1,8);
    CHECK(cairo_surface_status(surface)==CAIRO_STATUS_INVALID_SIZE);cairo_surface_destroy(surface);
    surface=cairo_image_surface_create(CAIRO_FORMAT_ARGB32,8,8);c=cairo_create(surface);
    cairo_restore(c);CHECK(cairo_status(c)==CAIRO_STATUS_INVALID_RESTORE);cairo_destroy(c);cairo_surface_destroy(surface);
    cairo_matrix_t matrix;cairo_matrix_init(&matrix,2,0.5,-0.25,3,7,-9);double x=11,y=-13;
    cairo_matrix_transform_point(&matrix,&x,&y);CHECK(cairo_matrix_invert(&matrix)==CAIRO_STATUS_SUCCESS);
    cairo_matrix_transform_point(&matrix,&x,&y);CHECK(fabs(x-11)<1e-10&&fabs(y+13)<1e-10);
    cairo_matrix_init_scale(&matrix,0,0);CHECK(cairo_matrix_invert(&matrix)==CAIRO_STATUS_INVALID_MATRIX);
    surface=cairo_recording_surface_create(CAIRO_CONTENT_COLOR_ALPHA,NULL);c=cairo_create(surface);
    cairo_set_source_rgb(c,1,0,0);cairo_rectangle(c,2,3,7,5);cairo_fill(c);
    double left,top,width,height;cairo_recording_surface_ink_extents(surface,&left,&top,&width,&height);
    CHECK(left==2&&top==3&&width==7&&height==5);cairo_destroy(c);cairo_surface_destroy(surface);
    puts("[ OK ] actual Cairo clip,integer alpha,stride,recording,matrix and error checks passed");return 0;
}
typedef struct {FT_Library library;FT_Face ft;cairo_font_face_t *cairo;} Font;
static int font_open(Font *f){
    memset(f,0,sizeof(*f));CHECK(!FT_Init_FreeType(&f->library));
    CHECK(!FT_New_Memory_Face(f->library,font_bytes,font_size,0,&f->ft));
    f->cairo=cairo_ft_font_face_create_for_ft_face(f->ft,FT_LOAD_DEFAULT);
    CHECK(cairo_font_face_status(f->cairo)==CAIRO_STATUS_SUCCESS);return 0;
}
static void font_release_face(Font *f){cairo_font_face_destroy(f->cairo);}
static void font_release_library(Font *f){FT_Done_Face(f->ft);FT_Done_FreeType(f->library);}
static int glyphs(cairo_t *c,Font *font,const char *text,double x,double y){
    cairo_set_font_face(c,font->cairo);cairo_set_font_size(c,24);
    cairo_glyph_t list[64];unsigned count=0;
    for(;*text&&count<64;++text,++count){list[count].index=FT_Get_Char_Index(font->ft,(unsigned char)*text);list[count].x=x+count*14;list[count].y=y;}
    CHECK(!*text);cairo_show_glyphs(c,list,count);CHECK(cairo_status(c)==CAIRO_STATUS_SUCCESS);return 0;
}
static int paint(uint32_t *pixels,unsigned epoch,Font *font){
    cairo_surface_t *surface=cairo_image_surface_create_for_data((unsigned char*)pixels,CAIRO_FORMAT_ARGB32,512,256,512*4);
    cairo_t *c=cairo_create(surface);cairo_pattern_t *gradient=cairo_pattern_create_linear(0,0,512,256);
    cairo_pattern_add_color_stop_rgb(gradient,0,0.85,0.93,1);cairo_pattern_add_color_stop_rgb(gradient,1,1,1,1);
    cairo_set_source(c,gradient);cairo_paint(c);cairo_pattern_destroy(gradient);
    cairo_save(c);cairo_rectangle(c,12,12,330,150);cairo_clip(c);
    gradient=cairo_pattern_create_radial(120,70,3,150,90,130);
    cairo_pattern_add_color_stop_rgb(gradient,0,1,0.9,0.1);cairo_pattern_add_color_stop_rgb(gradient,1,0.1,0.3,0.7);
    cairo_set_source(c,gradient);cairo_arc(c,150,85,95,0,6.283185307179586);cairo_fill(c);cairo_pattern_destroy(gradient);
    cairo_set_source_rgba(c,1,0.1,0.3,0.65);cairo_set_line_width(c,12);
    double dash[]={18,8};cairo_set_dash(c,dash,2,epoch*3);
    cairo_move_to(c,0,140);cairo_curve_to(c,70,-30,250,230,355,20);cairo_stroke(c);cairo_restore(c);
    cairo_save(c);cairo_translate(c,420,85);cairo_rotate(c,epoch*0.18);
    cairo_push_group(c);cairo_set_source_rgba(c,0.1,0.65,0.35,0.8);cairo_rectangle(c,-55,-40,110,80);cairo_fill(c);
    cairo_set_source_rgba(c,0.3,0.1,0.85,0.7);cairo_arc(c,0,0,35,0,6.283185307179586);cairo_fill(c);
    cairo_pop_group_to_source(c);cairo_paint_with_alpha(c,0.9);cairo_restore(c);
    uint32_t tile[64];for(unsigned i=0;i<64;++i)tile[i]=((i/8+i%8)&1)?0xff17365e:0xffb6d5f4;
    cairo_surface_t *pattern=cairo_image_surface_create_for_data((unsigned char*)tile,CAIRO_FORMAT_ARGB32,8,8,32);
    cairo_set_source_surface(c,pattern,0,0);cairo_pattern_set_extend(cairo_get_source(c),CAIRO_EXTEND_REPEAT);
    cairo_rectangle(c,350,165,145,70);cairo_fill(c);cairo_surface_destroy(pattern);
    cairo_set_source_rgb(c,0.05,0.1,0.2);CHECK(!glyphs(c,font,"Cairo native",18,210));
    CHECK(cairo_status(c)==CAIRO_STATUS_SUCCESS);cairo_surface_flush(surface);
    CHECK(cairo_surface_status(surface)==CAIRO_STATUS_SUCCESS);cairo_destroy(c);cairo_surface_destroy(surface);
    return 0;
}
typedef struct {Font font;int result;} Worker;
static void *worker(void *arg){
    Worker *w=arg;uint32_t *pixels=malloc(512*256*4);if(!pixels){w->result=1;return NULL;}
    for(unsigned i=1;i<=6;++i)if(paint(pixels,i,&w->font)){w->result=1;break;}
    free(pixels);return NULL;
}
static int concurrency(void){
    Worker workers[3]={0};pthread_t threads[3];pthread_attr_t attr;CHECK(!pthread_attr_init(&attr));
    CHECK(!pthread_attr_setstacksize(&attr,128*1024));
    for(unsigned i=0;i<3;++i)CHECK(!font_open(&workers[i].font));
    for(unsigned i=0;i<3;++i)CHECK(!pthread_create(&threads[i],&attr,worker,&workers[i]));
    for(unsigned i=0;i<3;++i)CHECK(!pthread_join(threads[i],NULL)&&!workers[i].result);
    CHECK(!pthread_attr_destroy(&attr));
    for(unsigned i=0;i<3;++i)font_release_face(&workers[i].font);
    /* Release original Cairo caches before freeing the caller-owned FT faces. */
    cairo_debug_reset_static_data();for(unsigned i=0;i<3;++i)font_release_library(&workers[i].font);
    puts("[ OK ] actual Cairo,pixman and FreeType native workers passed");return 0;
}
static Font main_font;
static int render(unsigned epoch){
    CHECK(!paint(cairo_frame,epoch,&main_font));uint64_t hash=UINT64_C(14695981039346656037);
    for(unsigned i=0;i<512*256;++i)for(unsigned shift=0;shift<32;shift+=8){hash^=(cairo_frame[i]>>shift)&255u;hash*=UINT64_C(1099511628211);}
    cairo_frame_hash=hash;printf("[CAIRO] epoch=%u hash=%016llx\n",epoch,(unsigned long long)hash);return 0;
}
#ifndef CHOS_CAIRO_HOST
static uint32_t window_id;
static void message(chos_gui_surface_message_t *m,unsigned type,size_t payload,uint64_t request){
    memset(m,0,sizeof(*m));m->header.version=CHOS_GUI_ABI_VERSION;m->header.type=type;
    m->header.size_bytes=offsetof(chos_gui_surface_message_t,payload)+payload;m->header.request_id=request;
}
static int present(unsigned epoch){
    CHECK(!render(epoch));chos_gui_surface_message_t m;message(&m,CHOS_GUI_MSG_SURFACE_BEGIN,sizeof(m.payload.begin),0);
    m.payload.begin.window_id=window_id;m.payload.begin.x=12;m.payload.begin.y=64;m.payload.begin.width=512;m.payload.begin.height=256;
    long id;unsigned retries=0;do{id=chos_gui_surface_submit(&m,m.header.size_bytes);if(id==-CHOS_ERRNO_EAGAIN){CHECK(++retries<200);chos_sleep_ticks(1);}}while(id==-CHOS_ERRNO_EAGAIN);CHECK(id>0);
    message(&m,CHOS_GUI_MSG_SURFACE_TILE,sizeof(m.payload.tile),id);m.payload.tile.window_id=window_id;m.payload.tile.pixel_count=128;
    for(unsigned tile=0;tile<1024;++tile){m.payload.tile.tile_index=tile;memcpy(m.payload.tile.pixels,cairo_frame+tile*128,512);CHECK(!chos_gui_surface_submit(&m,m.header.size_bytes));if(!(tile%32))chos_yield();}
    message(&m,CHOS_GUI_MSG_SURFACE_PRESENT,sizeof(m.payload.window),id);m.payload.window.window_id=window_id;CHECK(!chos_gui_surface_submit(&m,m.header.size_bytes));frames_submitted=epoch;return 0;
}
static int window(void){
    chos_gui_message_t m={0};m.header.version=CHOS_GUI_ABI_VERSION;m.header.type=CHOS_GUI_MSG_CREATE_WINDOW;
    m.header.size_bytes=offsetof(chos_gui_message_t,payload)+sizeof(m.payload.create_window);
    m.payload.create_window.x=100;m.payload.create_window.y=90;m.payload.create_window.width=600;m.payload.create_window.height=420;m.payload.create_window.background_argb=0xffe0e0e0;
    long id=chos_gui_submit(&m,m.header.size_bytes);CHECK(id>=100&&id<=103);window_id=id;
    memset(&m,0,sizeof(m));m.header.version=CHOS_GUI_ABI_VERSION;m.header.type=CHOS_GUI_MSG_SET_TITLE;
    m.header.size_bytes=offsetof(chos_gui_message_t,payload)+sizeof(m.payload.set_title);m.payload.set_title.window_id=window_id;
    const char *title="CHOS Native Cairo";m.payload.set_title.title_length=strlen(title);memcpy(m.payload.set_title.title,title,strlen(title));CHECK(!chos_gui_submit(&m,m.header.size_bytes));return 0;
}
#endif
int main(int argc,char **argv){
#ifdef CHOS_CAIRO_HOST
    CHECK(argc==3);CHECK(!font_load(argv[1]));
#else
    (void)argc;(void)argv;CHECK(!font_load(NULL));
#endif
    CHECK(!analytic());CHECK(!concurrency());CHECK(!font_open(&main_font));
#ifdef CHOS_CAIRO_HOST
    for(unsigned epoch=1;epoch<=3;++epoch){CHECK(!render(epoch));char path[512];snprintf(path,sizeof(path),"%s/frame%u.rgb",argv[2],epoch);
        FILE *f=fopen(path,"wb");CHECK(f);for(unsigned i=0;i<512*256;++i){unsigned char rgb[3]={cairo_frame[i]>>16,cairo_frame[i]>>8,cairo_frame[i]};CHECK(fwrite(rgb,1,3,f)==3);}CHECK(!fclose(f));}
#else
    CHECK(!window());CHECK(!present(1));
    for(;;){chos_gui_message_t event;long r=chos_gui_poll_event(&event,sizeof(event));if(r==-CHOS_ERRNO_EAGAIN){chos_sleep_ticks(1);continue;}CHECK(r>=0);
        if(event.header.type==CHOS_GUI_MSG_CLOSE_REQUEST)break;
        if(event.header.type==CHOS_GUI_MSG_KEY_EVENT&&(event.payload.key_event.event_flags&CHOS_GUI_EVENT_KEY_DOWN)){
            if(event.payload.key_event.keycode==1)break;if(event.payload.key_event.keycode==57)CHECK(!present(frames_submitted+1));}}
    chos_gui_message_t close={0};close.header.version=CHOS_GUI_ABI_VERSION;close.header.type=CHOS_GUI_MSG_DESTROY_WINDOW;
    close.header.size_bytes=offsetof(chos_gui_message_t,payload)+sizeof(close.payload.window);close.payload.window.window_id=window_id;CHECK(!chos_gui_submit(&close,close.header.size_bytes));
#endif
    font_release_face(&main_font);cairo_debug_reset_static_data();font_release_library(&main_font);
#ifdef CHOS_CAIRO_HOST
    free((void*)font_bytes);
#endif
    puts("[ OK ] actual Cairo application and font caches cleaned up");return 0;
}
