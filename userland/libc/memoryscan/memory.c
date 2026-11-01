/* Original scanf conversions use a bounded, immutable input view. Unlike the
 * strto* lookahead view, this boundary excludes NUL: scanf observes EOF there. */
#include <stdio.h>
#include <string.h>
#include <errno.h>
int vsscanf(const char *restrict source,const char *restrict format,va_list args){
 if(!source||!format){errno=EINVAL;return EOF;}
 FILE view={.buf=(unsigned char *)source,.rpos=(unsigned char *)source,
  .rend=(unsigned char *)source+strlen(source)};
 return vfscanf(&view,format,args);
}
