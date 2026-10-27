#ifndef CHOS_FILE_POSITION_H
#define CHOS_FILE_POSITION_H
#include <stdint.h>
#include "chos/syscall.h"
#include "chos/file_api.h"
/* Calculate a signed-ABI file position, including a valid position beyond EOF.
 * On error the caller's current offset and output remain unchanged. */
static inline int chos_file_position(uint64_t current,uint64_t size,int64_t delta,unsigned origin,uint64_t *out){
 uint64_t base;
 if(!out)return -CHOS_ERRNO_EINVAL;
 if(origin==CHOS_SEEK_SET)base=0;
 else if(origin==CHOS_SEEK_CUR)base=current;
 else if(origin==CHOS_SEEK_END)base=size;
 else return -CHOS_ERRNO_EINVAL;
 if(base>INT64_MAX)return -CHOS_ERRNO_EOVERFLOW;
 uint64_t next;
 if(delta<0){uint64_t magnitude=(uint64_t)(-(delta+1))+1;if(magnitude>base)return -CHOS_ERRNO_EINVAL;next=base-magnitude;}
 else {if((uint64_t)delta>(uint64_t)INT64_MAX-base)return -CHOS_ERRNO_EOVERFLOW;next=base+(uint64_t)delta;}
 *out=next;return 0;
}
#endif
