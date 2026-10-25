#ifndef CHOS_SCROLLBACK_H
#define CHOS_SCROLLBACK_H
#include <stddef.h>
/* Fixed storage, bounded copies, no allocation. Offset counts rows above live tail.
 * Push preserves an already scrolled viewport until its oldest row is evicted. */
typedef struct {
 char *lines;
 size_t width,capacity,count,start,offset;
} chos_scrollback_t;
static inline void chos_scrollback_clear(chos_scrollback_t *h){h->count=h->start=h->offset=0;}
static inline void chos_scrollback_push(chos_scrollback_t *h,const char *line){
 if(!h->capacity||!h->width)return;
 size_t slot=(h->start+h->count)%h->capacity;
 if(h->count==h->capacity){slot=h->start;h->start=(h->start+1)%h->capacity;}
 else ++h->count;
 char *out=h->lines+slot*h->width;size_t n=0;
 while(n+1<h->width&&line[n]){out[n]=line[n];++n;}out[n]=0;
 if(h->offset && h->offset<h->count)++h->offset;
}
static inline const char *chos_scrollback_at(const chos_scrollback_t *h,size_t row){
 return row<h->count?h->lines+((h->start+row)%h->capacity)*h->width:NULL;
}
static inline void chos_scrollback_move(chos_scrollback_t *h,int older,size_t rows){
 if(older){size_t available=h->count-h->offset;h->offset+=rows<available?rows:available;}
 else h->offset-=rows<h->offset?rows:h->offset;
}
#endif
