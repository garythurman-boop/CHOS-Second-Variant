#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "chos/scrollback.h"
int main(void){
 char rows[3][8];chos_scrollback_t h={.lines=&rows[0][0],.width=8,.capacity=3};
 chos_scrollback_push(&h,"one");chos_scrollback_push(&h,"two");
 chos_scrollback_move(&h,1,SIZE_MAX);assert(h.offset==2);
 chos_scrollback_push(&h,"three");assert(h.offset==3&&!strcmp(chos_scrollback_at(&h,0),"one"));
 chos_scrollback_push(&h,"four");assert(h.count==3&&h.offset==3&&!strcmp(chos_scrollback_at(&h,0),"two"));
 chos_scrollback_move(&h,0,SIZE_MAX);assert(h.offset==0);
 for(unsigned i=0;i<1000;i++)chos_scrollback_push(&h,"longer than the row");
 assert(h.count==3&&!strcmp(chos_scrollback_at(&h,2),"longer ")&&!chos_scrollback_at(&h,3));
 chos_scrollback_clear(&h);assert(!h.count&&!h.offset&&!h.start&&!chos_scrollback_at(&h,0));
 puts("scrollback: bounded eviction, anchored viewport, saturating navigation, clear and truncation passed");
}
