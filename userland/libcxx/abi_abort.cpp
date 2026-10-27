/* Diagnostic backend for LLVM's RTTI invariants, not a printf implementation. */
#include <cstdlib>
#include <cstring>
#include "chos/user.h"
extern "C" __attribute__((noreturn,format(printf,1,2))) void abort_message(const char *format,...){
 const char *prefix="libc++abi termination: ";chos_write(prefix,std::strlen(prefix));
 const char *s=format?format:"(null diagnostic)";chos_write(s,std::strlen(s));chos_write("\n",1);std::abort();
}
