#ifndef CHOS_GUI_SURFACE_STORE_H
#define CHOS_GUI_SURFACE_STORE_H
#include <stdint.h>
#include "chos/gui_surface.h"
#include "chos/syscall.h"

/* Kernel-private bounded double buffers. All mutations/read-pin operations are
 * serialized by the single-CPU GUI boundary. Pixel reads may run with IRQs on:
 * pins keep a retired front buffer immutable until that render finishes.
 * Storage persists across owners; no pixels become visible before full coverage.
 */
#define GUI_SURFACE_PIXELS (CHOS_GUI_SURFACE_MAX_WIDTH*CHOS_GUI_SURFACE_MAX_HEIGHT)
#define GUI_SURFACE_TILES (((CHOS_GUI_SURFACE_MAX_WIDTH+127u)/128u)*CHOS_GUI_SURFACE_MAX_HEIGHT)
typedef struct {
    uint64_t id;
    int32_t x,y;
    uint32_t width,height,buffer;
} gui_surface_frame_t;
typedef struct {
    uint32_t pixels[2][GUI_SURFACE_PIXELS];
    gui_surface_frame_t front,pending;
    uint64_t last_id,coverage[(GUI_SURFACE_TILES+63u)/64u];
    uint32_t covered,total_tiles,readers[2];
} gui_surface_store_t;

static inline int gui_surface_begin(gui_surface_store_t *s,uint64_t id,
                                    const chos_gui_surface_begin_t *b) {
    if(!id||id>INT64_MAX||id<=s->last_id||b->reserved||b->x<0||b->y<0||
       !b->width||!b->height||b->width>CHOS_GUI_SURFACE_MAX_WIDTH||b->height>CHOS_GUI_SURFACE_MAX_HEIGHT||
       (uint64_t)(uint32_t)b->x+b->width>INT32_MAX||(uint64_t)(uint32_t)b->y+b->height>INT32_MAX)
        return -CHOS_ERRNO_EINVAL;
    unsigned back=s->front.buffer^1u;
    if(s->readers[back])return -CHOS_ERRNO_EAGAIN;
    gui_surface_frame_t next={id,b->x,b->y,b->width,b->height,back};
    s->pending=next;s->last_id=id;s->covered=0;
    s->total_tiles=((b->width+127u)/128u)*b->height;
    for(unsigned i=0;i<(GUI_SURFACE_TILES+63u)/64u;++i)s->coverage[i]=0;
    return 0;
}
static inline int gui_surface_tile(gui_surface_store_t *s,uint64_t id,
                                   const chos_gui_surface_tile_t *t) {
    if(!id||id!=s->pending.id||t->reserved||t->tile_index>=s->total_tiles)return -CHOS_ERRNO_EINVAL;
    uint32_t across=(s->pending.width+127u)/128u;
    uint32_t x=(t->tile_index%across)*128u,y=t->tile_index/across;
    uint32_t count=s->pending.width-x;if(count>128u)count=128u;
    if(t->pixel_count!=count)return -CHOS_ERRNO_EINVAL;
    if(s->readers[s->pending.buffer])return -CHOS_ERRNO_EAGAIN;
    size_t start=(size_t)y*s->pending.width+x;
    for(uint32_t i=0;i<count;++i)s->pixels[s->pending.buffer][start+i]=t->pixels[i]|0xff000000u;
    uint64_t mask=UINT64_C(1)<<(t->tile_index%64u);
    if(!(s->coverage[t->tile_index/64u]&mask)){
        s->coverage[t->tile_index/64u]|=mask;++s->covered;
    }
    return 0;
}
static inline int gui_surface_present(gui_surface_store_t *s,uint64_t id) {
    if(!id||id!=s->pending.id)return -CHOS_ERRNO_EINVAL;
    if(s->covered!=s->total_tiles||s->readers[s->pending.buffer])return -CHOS_ERRNO_EAGAIN;
    s->front=s->pending;s->pending.id=0;
    return 0;
}
static inline void gui_surface_drop(gui_surface_store_t *s) {
    /* Pins/last ID survive reuse. No clearing/copying megabytes with IRQs masked. */
    s->front.id=0;s->pending.id=0;s->covered=0;s->total_tiles=0;
}
static inline int gui_surface_acquire(gui_surface_store_t *s,gui_surface_frame_t *out) {
    if(!s->front.id||s->readers[s->front.buffer]==UINT32_MAX)return 0;
    *out=s->front;++s->readers[out->buffer];return 1;
}
static inline int gui_surface_release(gui_surface_store_t *s,const gui_surface_frame_t *frame) {
    if(frame->buffer>1u||!s->readers[frame->buffer])return 0;
    --s->readers[frame->buffer];return 1;
}
#endif
