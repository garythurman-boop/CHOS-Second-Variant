#ifndef CHOS_FILE_API_H
#define CHOS_FILE_API_H

#include <stdint.h>

#define CHOS_FILE_OPEN_MAX 4u
#define CHOS_SEEK_SET 0u
#define CHOS_SEEK_CUR 1u
#define CHOS_SEEK_END 2u
#define CHOS_FILE_LIST_MAX 64u
#define CHOS_FILE_PATH_CAPACITY 64u
#define CHOS_FILE_FLAG_PERSISTENT (1u << 0)
#define CHOS_FILE_FLAG_READ_ONLY  (1u << 1)
#define CHOS_FILE_FLAG_EXECUTABLE (1u << 2)
#define CHOS_FILE_FLAG_TEXT       (1u << 3)
#define CHOS_FILE_FLAG_DIRECTORY  (1u << 4)

typedef struct {
    char path[CHOS_FILE_PATH_CAPACITY];
    uint32_t size_bytes;
    uint32_t flags;
} chos_file_list_entry_t;

_Static_assert(sizeof(chos_file_list_entry_t) == 72u, "EL0 file list entry ABI changed");

#define CHOS_FILE_INFO_VERSION 1u
#define CHOS_FILE_INFO_SNAPSHOT 1u
#define CHOS_FILE_INFO_READ_ONLY 2u
#define CHOS_FILE_INFO_CHFS_SOURCE 4u
#define CHOS_FILE_INFO_WORKING_COPY 8u
#define CHOS_FILE_INFO_DIRTY 16u
/* Process-owned file view, not POSIX inode metadata. SNAPSHOT/READ_ONLY identify
 * the immutable legacy open; WORKING_COPY identifies a mutable private view,
 * with DIRTY set until conditional commit. Source is the open-time origin. */
typedef struct {
 uint32_t version,size_bytes,flags,reserved0;
 uint64_t length,position;
 uint64_t reserved[4];
} chos_file_info_t;
typedef struct {
 uint64_t offset,buffer;
 uint32_t length,flags;
 uint64_t reserved;
} chos_file_read_at_t;
_Static_assert(sizeof(chos_file_info_t)==64u,"file information ABI size");
_Static_assert(sizeof(chos_file_read_at_t)==32u,"file positional-read ABI size");
#endif
