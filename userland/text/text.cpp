#include <chos/text.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb.h>
#include <hb-ft.h>
#if CHOS_TEXT_WITH_ICU
#include <hb-icu.h>
#endif
#include <pthread.h>
#include <cstdlib>
#include <cstring>
#include <climits>
struct chos_text_font {
    FT_Library library;
    FT_Face face;
    hb_font_t *shaper;
    unsigned char *bytes;
    pthread_mutex_t mutex;
};
static pthread_mutex_t registry=PTHREAD_MUTEX_INITIALIZER;
static unsigned activeFonts;
static void releaseSlot() {pthread_mutex_lock(&registry);--activeFonts;pthread_mutex_unlock(&registry);}
extern "C" int chos_text_font_open(const void *bytes,size_t length,unsigned index,chos_text_font_t **out) {
    if(!out)return CHOS_TEXT_INVALID;
    *out=nullptr;
    if(!bytes||!length||length>CHOS_TEXT_FONT_BYTES_MAX||index>65535u)return CHOS_TEXT_INVALID;
    pthread_mutex_lock(&registry);
    if(activeFonts==CHOS_TEXT_FONTS_MAX){pthread_mutex_unlock(&registry);return CHOS_TEXT_LIMIT;}
    ++activeFonts;pthread_mutex_unlock(&registry);
    auto f=static_cast<chos_text_font_t*>(std::calloc(1,sizeof(chos_text_font_t)));
    if(!f){releaseSlot();return CHOS_TEXT_NOMEM;}
    if(pthread_mutex_init(&f->mutex,nullptr)){std::free(f);releaseSlot();return CHOS_TEXT_NOMEM;}
    f->bytes=static_cast<unsigned char*>(std::malloc(length));
    int error=CHOS_TEXT_NOMEM;
    if(f->bytes){
        std::memcpy(f->bytes,bytes,length);
        if(!FT_Init_FreeType(&f->library)){
            error=CHOS_TEXT_BAD_FONT;
            auto faceError=FT_New_Memory_Face(f->library,f->bytes,static_cast<FT_Long>(length),static_cast<FT_Long>(index),&f->face);
            if(faceError==FT_Err_Out_Of_Memory)error=CHOS_TEXT_NOMEM;
            if(!faceError){
                // Only scalable outlines are accepted by this first renderer.
                // Embedded/color/SVG bitmap fonts need separate decoder work.
                auto sizeError=FT_IS_SCALABLE(f->face)?FT_Set_Pixel_Sizes(f->face,0,24):FT_Err_Invalid_Face_Handle;
                if(sizeError==FT_Err_Out_Of_Memory)error=CHOS_TEXT_NOMEM;
                if(!sizeError){
                    f->shaper=hb_ft_font_create_referenced(f->face);
                    if(f->shaper&&f->shaper!=hb_font_get_empty()){
                        hb_ft_font_set_load_flags(f->shaper,FT_LOAD_DEFAULT|FT_LOAD_NO_BITMAP);
                        *out=f;return CHOS_TEXT_OK;
                    }
                    error=CHOS_TEXT_NOMEM;
                }
            }
        }
    }
    if(f->shaper)hb_font_destroy(f->shaper);
    if(f->face)FT_Done_Face(f->face);
    if(f->library)FT_Done_FreeType(f->library);
    std::free(f->bytes);pthread_mutex_destroy(&f->mutex);std::free(f);releaseSlot();return error;
}
extern "C" void chos_text_font_close(chos_text_font_t *f) {
    if(!f)return;
    hb_font_destroy(f->shaper);FT_Done_Face(f->face);FT_Done_FreeType(f->library);
    std::free(f->bytes);pthread_mutex_destroy(&f->mutex);std::free(f);releaseSlot();
}
extern "C" int chos_text_font_pixels(chos_text_font_t *f,unsigned height) {
    if(!f||!height||height>128)return CHOS_TEXT_INVALID;
    pthread_mutex_lock(&f->mutex);auto error=FT_Set_Pixel_Sizes(f->face,0,height);
    if(!error)hb_ft_font_changed(f->shaper);
    pthread_mutex_unlock(&f->mutex);return error==FT_Err_Out_Of_Memory?CHOS_TEXT_NOMEM:error?CHOS_TEXT_RASTER:CHOS_TEXT_OK;
}
static int64_t floor64(int64_t n) {return n/64-(n%64<0);}
static uint32_t blend(uint32_t background,uint32_t ink,unsigned coverage) {
    uint32_t out=0xff000000;
    for(unsigned shift=0;shift<=16;shift+=8)
        out|=((((ink>>shift)&255)*coverage+((background>>shift)&255)*(255-coverage)+127)/255)<<shift;
    return out;
}
extern "C" int chos_text_draw(chos_text_font_t *f,const char *text,size_t length,
                 const char *language,unsigned direction,chos_text_surface_t *surface,
                 int32_t x,int32_t baseline,uint32_t color,chos_text_metrics_t *metrics) {
    if(!f||!text||length>CHOS_TEXT_UTF8_MAX||direction>CHOS_TEXT_RTL||!surface||!surface->pixels||
       !surface->width||surface->width>1024||!surface->height||surface->height>768||
       surface->stride<surface->width||surface->stride>4096||
       (uint64_t)surface->stride*surface->height>surface->capacity_pixels)return CHOS_TEXT_INVALID;
    size_t languageLength=language?strnlen(language,64):0;if(languageLength==64)return CHOS_TEXT_INVALID;
    for(size_t i=0;i<languageLength;++i)
        if(!((language[i]>='a'&&language[i]<='z')||(language[i]>='A'&&language[i]<='Z')||
             (language[i]>='0'&&language[i]<='9')||language[i]=='-'))return CHOS_TEXT_INVALID;
    pthread_mutex_lock(&f->mutex);
    auto buffer=hb_buffer_create();int result=CHOS_TEXT_NOMEM;
    if(!hb_buffer_allocation_successful(buffer)){hb_buffer_destroy(buffer);pthread_mutex_unlock(&f->mutex);return result;}
#if CHOS_TEXT_WITH_ICU
    hb_buffer_set_unicode_funcs(buffer,hb_icu_get_unicode_funcs());
#endif
    if(languageLength)hb_buffer_set_language(buffer,hb_language_from_string(language,static_cast<int>(languageLength)));
    hb_buffer_add_utf8(buffer,text,static_cast<int>(length),0,static_cast<int>(length));
    if(direction)hb_buffer_set_direction(buffer,direction==CHOS_TEXT_LTR?HB_DIRECTION_LTR:HB_DIRECTION_RTL);
    hb_buffer_guess_segment_properties(buffer);
    if(!hb_shape_full(f->shaper,buffer,nullptr,0,nullptr)||!hb_buffer_allocation_successful(buffer))result=CHOS_TEXT_SHAPE;
    else {
        unsigned count=0;auto info=hb_buffer_get_glyph_infos(buffer,&count);auto positions=hb_buffer_get_glyph_positions(buffer,nullptr);
        if(count>CHOS_TEXT_GLYPHS_MAX)result=CHOS_TEXT_LIMIT;
        else {
            chos_text_metrics_t observed{};observed.glyphs=count;
            observed.direction=hb_buffer_get_direction(buffer)==HB_DIRECTION_RTL?CHOS_TEXT_RTL:CHOS_TEXT_LTR;
            int64_t penX=0,penY=0;result=CHOS_TEXT_OK;
            for(unsigned g=0;g<count;++g){
                auto rasterError=FT_Load_Glyph(f->face,info[g].codepoint,FT_LOAD_DEFAULT|FT_LOAD_NO_BITMAP);
                if(!rasterError)rasterError=FT_Render_Glyph(f->face->glyph,FT_RENDER_MODE_NORMAL);
                if(rasterError){result=rasterError==FT_Err_Out_Of_Memory?CHOS_TEXT_NOMEM:CHOS_TEXT_RASTER;break;}
                auto slot=f->face->glyph;auto& bitmap=slot->bitmap;
                int64_t pitch=bitmap.pitch,absPitch=pitch<0?-pitch:pitch;
                if(bitmap.width>4096||bitmap.rows>4096||
                   (bitmap.pixel_mode!=FT_PIXEL_MODE_GRAY&&bitmap.pixel_mode!=FT_PIXEL_MODE_MONO)||
                   (bitmap.width&&bitmap.rows&&(!bitmap.buffer||
                   absPitch<(bitmap.pixel_mode==FT_PIXEL_MODE_MONO?(bitmap.width+7)/8:bitmap.width)))){
                    result=CHOS_TEXT_RASTER;break;
                }
                int64_t left=(int64_t)x+floor64(penX+positions[g].x_offset)+slot->bitmap_left;
                int64_t top=(int64_t)baseline-floor64(penY+positions[g].y_offset)-slot->bitmap_top;
                for(unsigned row=0;row<bitmap.rows;++row){
                    int64_t dy=top+row;if(dy<0||dy>=surface->height)continue;
                    const unsigned char *mask=bitmap.buffer+(pitch<0?(bitmap.rows-1-row)*absPitch:row*absPitch);
                    for(unsigned column=0;column<bitmap.width;++column){
                        int64_t dx=left+column;if(dx<0||dx>=surface->width)continue;
                        unsigned coverage=bitmap.pixel_mode==FT_PIXEL_MODE_MONO?((mask[column/8]>>(7-column%8))&1u)*255u:
                            bitmap.num_grays>1?mask[column]*255u/(bitmap.num_grays-1u):0;
                        if(coverage>255)coverage=255;
                        if(coverage){auto& pixel=surface->pixels[(size_t)dy*surface->stride+(size_t)dx];pixel=blend(pixel,color,coverage);++observed.ink_pixels;}
                    }
                }
                penX+=positions[g].x_advance;penY+=positions[g].y_advance;
            }
            observed.advance_x_26_6=penX;observed.advance_y_26_6=penY;
            if(!result&&metrics)*metrics=observed;
        }
    }
    hb_buffer_destroy(buffer);pthread_mutex_unlock(&f->mutex);return result;
}
