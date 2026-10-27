/* Real bounded string scanner for musl floatscan, not a filesystem stream.
 * sh_fromstring initially supplies the original musl sentinel.  Replace it
 * with a valid one-past-NUL boundary before any scan operation.  Keeping NUL
 * readable preserves the original scanner's lookahead/pushback semantics. */
#include <stdio.h>
#include "shgetc.h"
#include <string.h>
#include <stdlib.h>
void __shlim(FILE *f,off_t limit){
 if(limit<0)abort();
 if(f->rend==(void *)-1)f->rend=f->buf+strlen((const char *)f->buf)+1;
 f->shlim=limit;f->shcnt=f->buf-f->rpos;
 f->shend=limit&&(off_t)(f->rend-f->rpos)>limit?f->rpos+limit:f->rend;
}
int __shgetc(FILE *f){
 /* The entire string is already buffered; reaching the boundary or an
  * explicit field limit is EOF. There is no I/O refill or success stub. */
 f->shend=f->rpos;f->shlim=-1;return EOF;
}
