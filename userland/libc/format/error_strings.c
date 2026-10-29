/* Public invariant-C error strings reuse the maintained private message table.
 * Returned messages are immutable static strings; no locale or shared scratch. */
#include <string.h>
#include <errno.h>
extern char *__chos_format_strerror(int);
char *strerror(int error){return __chos_format_strerror(error);}
int strerror_r(int error,char *out,size_t capacity){
    if(!out&&capacity)return EINVAL;
    const char *message=__chos_format_strerror(error);
    size_t length=strlen(message);
    if(!capacity)return ERANGE;
    size_t copied=length<capacity?length:capacity-1;
    memcpy(out,message,copied);out[copied]=0;
    return length<capacity?0:ERANGE;
}
