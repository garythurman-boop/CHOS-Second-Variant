#include <__verbose_abort>
#include <stdarg.h>
#include <cstdlib>
#include <cstring>
#include "chos/user.h"
_LIBCPP_BEGIN_NAMESPACE_STD
// Diagnostic backend only, not printf. Recognized formats are %s and %%.
// Unknown conversions print the literal template without consuming arguments.
void __libcpp_verbose_abort(const char *format,...){
 const char *s=format?format:"(null diagnostic)";bool supported=true;
 for(const char *p=s;*p;++p)if(*p=='%'){++p;if(*p!='s'&&*p!='%'){supported=false;break;}}
 chos_write("libc++ termination: ",20);
 if(!supported)chos_write(s,::strlen(s));
 else{
  va_list args;va_start(args,format);
  while(*s){const char *start=s;while(*s&&*s!='%')++s;chos_write(start,size_t(s-start));
   if(*s){++s;if(*s=='s'){const char *v=va_arg(args,const char *);if(!v)v="(null)";chos_write(v,::strlen(v));}else chos_write("%",1);++s;}
  }
  va_end(args);
 }
 chos_write("\n",1);::abort();
}
_LIBCPP_END_NAMESPACE_STD
