#include <cairo.h>
#include <cairo-ft.h>
#include <fontconfig/fontconfig.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#ifdef __CHOS__
#include <chos/libc.h>
#endif
#if !CAIRO_HAS_FC_FONT || !CAIRO_HAS_FT_FONT
#error Actual Cairo Fontconfig and FreeType backends are required
#endif
#define CHECK(x) do {if(!(x)){printf("[FAIL] Cairo Fontconfig line %u: %s\n",__LINE__,#x);fflush(stdout);return 1;}}while(0)
enum {WIDTH=256,HEIGHT=112,STRIDE=WIDTH*4};
static const char *font_names[]={"NotoSans-Regular.ttf","NotoNaskhArabic-Regular.ttf","NotoSansDevanagari-Regular.ttf"};
static char font_paths[3][512];
static unsigned char *reference_bytes[3];
static size_t reference_sizes[3];
static FcConfig *catalogue;
static size_t comparisons,pixel_count,glyph_count;
typedef struct {FT_Library library;FT_Face ft;} ExplicitFace;
static cairo_user_data_key_t explicit_key;
static void explicit_destroy(void *argument){
    ExplicitFace *owned=argument;FT_Done_Face(owned->ft);FT_Done_FreeType(owned->library);free(owned);
}
static int explicit_create(cairo_font_face_t **out,const char *path,int index){
    ExplicitFace *owned=calloc(1,sizeof(*owned));CHECK(owned);
    unsigned which=0;while(which<3&&strcmp(path,font_paths[which]))++which;CHECK(which<3);
    CHECK(!FT_Init_FreeType(&owned->library));
    /* The independent reference borrows immutable, preloaded font bytes. Its
     * cache-retained faces must not consume the production file-FD budget. */
    CHECK(!FT_New_Memory_Face(owned->library,reference_bytes[which],reference_sizes[which],index,&owned->ft));
    *out=cairo_ft_font_face_create_for_ft_face(owned->ft,0);
    CHECK(cairo_font_face_status(*out)==CAIRO_STATUS_SUCCESS);
    /* The callback couples both caller-owned FT objects to Cairo's last
     * reference, including references retained by its original font caches. */
    CHECK(cairo_font_face_set_user_data(*out,&explicit_key,owned,explicit_destroy)==CAIRO_STATUS_SUCCESS);
    return 0;
}
static int catalogue_create(int argc,char **argv){
#ifdef __CHOS__
    (void)argc;(void)argv;
    for(unsigned i=0;i<3;++i)snprintf(font_paths[i],sizeof(font_paths[i]),"/FONTS/F%u.TTF",i);
#else
    CHECK(argc==2);
    for(unsigned i=0;i<3;++i)CHECK(snprintf(font_paths[i],sizeof(font_paths[i]),"%s/%s",argv[1],font_names[i])<(int)sizeof(font_paths[i]));
#endif
    catalogue=FcConfigCreate();CHECK(catalogue);
    const FcChar8 rules[]="<fontconfig><alias><family>sans-serif</family><prefer><family>Noto Sans</family></prefer></alias></fontconfig>";
    CHECK(FcConfigParseAndLoadFromMemory(catalogue,rules,FcTrue));
    for(unsigned i=0;i<3;++i){
        CHECK(FcConfigAppFontAddFile(catalogue,(FcChar8*)font_paths[i]));
        FILE *file=fopen(font_paths[i],"rb");CHECK(file&&!fseek(file,0,SEEK_END));
        long size=ftell(file);CHECK(size>0&&size<=4*1024*1024&&!fseek(file,0,SEEK_SET));
        reference_bytes[i]=malloc((size_t)size);CHECK(reference_bytes[i]);reference_sizes[i]=(size_t)size;
        CHECK(fread(reference_bytes[i],1,(size_t)size,file)==(size_t)size&&!fclose(file));
    }
    CHECK(FcConfigSetCurrent(catalogue));return 0;
}
static cairo_font_options_t *make_options(void){
    cairo_font_options_t *options=cairo_font_options_create();
    cairo_font_options_set_antialias(options,CAIRO_ANTIALIAS_GRAY);
    cairo_font_options_set_hint_style(options,CAIRO_HINT_STYLE_NONE);
    cairo_font_options_set_hint_metrics(options,CAIRO_HINT_METRICS_OFF);
    return options;
}
static int missing_file(void){
    FcPattern *pattern=FcPatternCreate();CHECK(pattern);
    CHECK(FcPatternAddString(pattern,FC_FILE,(const FcChar8*)"/MISSING-CAIRO-FONT.TTF"));
    cairo_font_face_t *face=cairo_ft_font_face_create_for_pattern(pattern);
    CHECK(cairo_font_face_status(face)==CAIRO_STATUS_FILE_NOT_FOUND);
    cairo_font_face_destroy(face);FcPatternDestroy(pattern);return 0;
}
static int configure_pattern(FcPattern *pattern,cairo_font_options_t *options){
    CHECK(FcConfigSubstitute(catalogue,pattern,FcMatchPattern));
    cairo_ft_font_options_substitute(options,pattern);FcDefaultSubstitute(pattern);
    FcBool antialias;int hint_style;
    CHECK(FcPatternGetBool(pattern,FC_ANTIALIAS,0,&antialias)==FcResultMatch&&antialias);
    CHECK(FcPatternGetInteger(pattern,FC_HINT_STYLE,0,&hint_style)==FcResultMatch&&hint_style==FC_HINT_NONE);
    return 0;
}
typedef struct {uint32_t storage[2+WIDTH*HEIGHT];cairo_text_extents_t extents;cairo_font_extents_t font;int glyphs;} Paint;
static int paint(cairo_font_face_t *face,cairo_font_options_t *options,const char *text,Paint *out){
    out->storage[0]=out->storage[1+WIDTH*HEIGHT]=0xa5abcdef;
    cairo_surface_t *surface=cairo_image_surface_create_for_data((unsigned char*)(out->storage+1),CAIRO_FORMAT_ARGB32,WIDTH,HEIGHT,STRIDE);
    CHECK(cairo_surface_status(surface)==CAIRO_STATUS_SUCCESS);cairo_t *context=cairo_create(surface);
    cairo_set_operator(context,CAIRO_OPERATOR_SOURCE);cairo_set_source_rgb(context,1,1,1);cairo_paint(context);
    cairo_set_operator(context,CAIRO_OPERATOR_OVER);cairo_set_source_rgb(context,0.08,0.18,0.3);
    cairo_set_font_options(context,options);cairo_set_font_face(context,face);cairo_set_font_size(context,27);
    cairo_text_extents(context,text,&out->extents);cairo_font_extents(context,&out->font);
    cairo_move_to(context,9,43);cairo_show_text(context,text);
    cairo_scaled_font_t *scaled=cairo_get_scaled_font(context);CHECK(cairo_scaled_font_status(scaled)==CAIRO_STATUS_SUCCESS);
    cairo_glyph_t *glyphs=NULL;int count=0;
    CHECK(cairo_scaled_font_text_to_glyphs(scaled,9,86,text,-1,&glyphs,&count,NULL,NULL,NULL)==CAIRO_STATUS_SUCCESS&&count>0);
    for(int i=0;i<count;++i)CHECK(glyphs[i].index);
    cairo_show_glyphs(context,glyphs,count);cairo_glyph_free(glyphs);out->glyphs=count;
    CHECK(cairo_status(context)==CAIRO_STATUS_SUCCESS);cairo_surface_flush(surface);
    CHECK(cairo_surface_status(surface)==CAIRO_STATUS_SUCCESS);
    CHECK(out->storage[0]==0xa5abcdef&&out->storage[1+WIDTH*HEIGHT]==0xa5abcdef);
    cairo_destroy(context);cairo_surface_destroy(surface);return 0;
}
static int equal_metrics(const Paint *a,const Paint *b){
#define METRIC(group,field) CHECK(fabs(a->group.field-b->group.field)<1e-10)
    METRIC(extents,x_bearing);METRIC(extents,y_bearing);METRIC(extents,width);
    METRIC(extents,height);METRIC(extents,x_advance);METRIC(extents,y_advance);
    METRIC(font,ascent);METRIC(font,descent);METRIC(font,height);
    METRIC(font,max_x_advance);METRIC(font,max_y_advance);
#undef METRIC
    return 0;
}
static int compare_case(const char *family,const char *text,FcChar32 character,unsigned expected_font,int lazy,size_t *count,size_t *pixels,size_t *glyphs){
    cairo_font_options_t *options=make_options();CHECK(cairo_font_options_status(options)==CAIRO_STATUS_SUCCESS);
    FcPattern *request=FcPatternCreate();CHECK(request&&FcPatternAddString(request,FC_FAMILY,(FcChar8*)family));
    CHECK(FcPatternAddDouble(request,FC_PIXEL_SIZE,27));FcCharSet *coverage=FcCharSetCreate();
    CHECK(coverage&&FcCharSetAddChar(coverage,character)&&FcPatternAddCharSet(request,FC_CHARSET,coverage));FcCharSetDestroy(coverage);
    CHECK(!configure_pattern(request,options));FcResult result;FcPattern *matched=FcFontMatch(catalogue,request,&result);
    CHECK(matched&&result==FcResultMatch);FcChar8 *selected=NULL;int index=0;
    CHECK(FcPatternGetString(matched,FC_FILE,0,&selected)==FcResultMatch&&!strcmp((char*)selected,font_paths[expected_font]));
    CHECK(FcPatternGetInteger(matched,FC_INDEX,0,&index)==FcResultMatch);
    cairo_font_face_t *explicit=NULL;CHECK(!explicit_create(&explicit,(char*)selected,index));
    cairo_font_face_t *face=cairo_ft_font_face_create_for_pattern(lazy?request:matched);
    CHECK(cairo_font_face_status(face)==CAIRO_STATUS_SUCCESS);
    /* Cairo must own everything it needs after callers release both patterns. */
    FcPatternDestroy(matched);FcPatternDestroy(request);
    cairo_font_face_t *retained=cairo_font_face_reference(face);cairo_font_face_destroy(face);
    Paint *a=malloc(sizeof(*a)),*b=malloc(sizeof(*b));CHECK(a&&b);
    CHECK(!paint(retained,options,text,a)&&!paint(explicit,options,text,b));
    CHECK(!memcmp(a->storage+1,b->storage+1,WIDTH*HEIGHT*4)&&!equal_metrics(a,b)&&a->glyphs==b->glyphs);
    unsigned ink=0;for(unsigned i=0;i<WIDTH*HEIGHT;++i)if(a->storage[1+i]!=0xffffffff)++ink;CHECK(ink>20);
    ++*count;*pixels+=WIDTH*HEIGHT;*glyphs+=(size_t)a->glyphs;
    cairo_font_face_destroy(retained);cairo_font_face_destroy(explicit);
    free(a);free(b);cairo_font_options_destroy(options);
    return 0;
}
static int cases(size_t *count,size_t *pixels,size_t *glyphs){
    CHECK(!compare_case("sans-serif","CHOS café 123",'A',0,0,count,pixels,glyphs));
    CHECK(!compare_case("Noto Sans","CHOS café 123",'A',0,1,count,pixels,glyphs));
    CHECK(!compare_case("Noto Sans","\xd8\xa7\xd8\xa8",0x0627,1,0,count,pixels,glyphs));
    CHECK(!compare_case("Noto Sans","\xe0\xa4\x95\xe0\xa4\x96",0x0915,2,0,count,pixels,glyphs));return 0;
}
static int descriptor_headroom(void){
    /* Three distinct production font files have already been selected. Cairo
     * must evict real unused FT file faces to leave the native service's two
     * remaining descriptors available to the application. */
    FILE *first=fopen(font_paths[0],"rb");CHECK(first);
    FILE *second=fopen(font_paths[1],"rb");CHECK(second);
    CHECK(!fclose(second)&&!fclose(first));return 0;
}
typedef struct {size_t count,pixels,glyphs;int failed;} Worker;
static void *worker(void *argument){Worker *w=argument;for(unsigned i=0;i<3;++i)if(cases(&w->count,&w->pixels,&w->glyphs)){w->failed=1;break;}return NULL;}
static int round_trip(int argc,char **argv){
    CHECK(!catalogue_create(argc,argv));CHECK(!missing_file()&&!cases(&comparisons,&pixel_count,&glyph_count));
    CHECK(!descriptor_headroom());
    Worker workers[3]={0};pthread_t threads[3];pthread_attr_t attr;
    CHECK(!pthread_attr_init(&attr)&&!pthread_attr_setstacksize(&attr,128*1024));
    for(unsigned i=0;i<3;++i)CHECK(!pthread_create(&threads[i],&attr,worker,&workers[i]));
    for(unsigned i=0;i<3;++i){CHECK(!pthread_join(threads[i],NULL)&&!workers[i].failed);comparisons+=workers[i].count;pixel_count+=workers[i].pixels;glyph_count+=workers[i].glyphs;}
    CHECK(!pthread_attr_destroy(&attr));cairo_debug_reset_static_data();FcConfigDestroy(catalogue);FcFini();
    for(unsigned i=0;i<3;++i){free(reference_bytes[i]);reference_bytes[i]=NULL;reference_sizes[i]=0;}
    catalogue=NULL;return 0;
}
int main(int argc,char **argv){
#ifdef __CHOS__
    chos_heap_stats_t baseline={0},stats;
#endif
    for(unsigned round=0;round<3;++round){
        CHECK(!round_trip(argc,argv));
#ifdef __CHOS__
        CHECK(!chos_libc_heap_info(&stats));
        printf("[CAIRO FC ROUND] round=%u allocations=%lu mapped=%lu\n",round,(unsigned long)stats.allocations,(unsigned long)stats.mapped_bytes);
        if(!round)baseline=stats;
        else CHECK(stats.allocations==baseline.allocations&&stats.mapped_bytes==baseline.mapped_bytes);
#endif
    }
    puts("[ OK ] actual Cairo Fontconfig pattern and explicit FreeType pixels,metrics and lifetime passed");
#ifdef __CHOS__
    printf("[CAIRO FC HEAP] allocations=%lu mapped=%lu\n",(unsigned long)stats.allocations,(unsigned long)stats.mapped_bytes);
#endif
    printf("[CAIRO FC] comparisons=%zu pixels=%zu glyphs=%zu\n",comparisons,pixel_count,glyph_count);
    puts("[ OK ] actual Cairo Fontconfig workers and cleanup passed");return 0;
}
