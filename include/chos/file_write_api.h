#ifndef CHOS_FILE_WRITE_API_H
#define CHOS_FILE_WRITE_API_H
#include <stdint.h>
#define CHOS_FILE_WRITE_VERSION 1u
#define CHOS_FILE_WRITE_CREATE 1u
#define CHOS_FILE_WRITE_EXCLUSIVE 2u
#define CHOS_FILE_WRITE_TRUNCATE 4u
#define CHOS_FILE_WRITE_FLAGS 7u
#define CHOS_FILE_WRITE_MAX_BYTES (4u*1024u*1024u)
#define CHOS_FILE_OWNED_BUDGET (8u*1024u*1024u)
/* A private read/write working copy. Creation/truncation is committed during
 * open; later writes/truncate affect only this handle until flush/close.
 * Flush rejects a changed source instead of overwriting newer contents.
 * Failed close retains the handle and dirty bytes; explicit abort discards
 * uncommitted changes. Process exit aborts, without implicit privileged writes.
 * Capacity is reserved up front, 1..4MiB, within the owned-buffer budget. */
typedef struct {
 uint32_t version,size_bytes,flags,capacity;
 uint64_t reserved[2];
} chos_file_open_write_t;
_Static_assert(sizeof(chos_file_open_write_t)==32u,"file working-copy open ABI");
#endif
