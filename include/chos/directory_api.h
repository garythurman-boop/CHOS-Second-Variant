#ifndef CHOS_DIRECTORY_API_H
#define CHOS_DIRECTORY_API_H
#include <stdint.h>
#include "chos/file_api.h"
#define CHOS_DIRECTORY_VERSION 1u
#define CHOS_DIRECTORY_PAGE_ENTRIES 8u
#define CHOS_DIRECTORY_START 1u
#define CHOS_DIRECTORY_END 2u
#define CHOS_DIRECTORY_READ_ONLY 4u
#define CHOS_DIRECTORY_CHFS_PRESENT 8u
#define CHOS_DIRECTORY_FLAGS 15u
/* Initialize version/size/START and zero the remaining fields. A successful
 * page replaces the whole record. Pass it back unchanged for continuation;
 * index is the next sorted entry. Any namespace/source-state change returns
 * ESTALE with output unchanged: discard partial results and restart. Tokens
 * are read-only consistency observations, not capabilities or persistent IDs.
 * count/total/entries are output-only. Reserved fields must remain zero. */
typedef struct {
 uint32_t version,size_bytes,flags,count;
 uint64_t mount,revision,index,total;
 uint64_t reserved[2];
 chos_file_list_entry_t entries[CHOS_DIRECTORY_PAGE_ENTRIES];
} chos_directory_page_t;
_Static_assert(sizeof(chos_directory_page_t)==640u,"directory page ABI size");
#endif
