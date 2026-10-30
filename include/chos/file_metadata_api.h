#ifndef CHOS_FILE_METADATA_API_H
#define CHOS_FILE_METADATA_API_H
#include <stdint.h>
#define CHOS_FILE_METADATA_VERSION 1u
#define CHOS_FILE_SOURCE_CHFS 1u
#define CHOS_FILE_SOURCE_ROMFS 2u
#define CHOS_FILE_SOURCE_NAMESPACE 3u
#define CHOS_FILE_META_REGULAR 1u
#define CHOS_FILE_META_DIRECTORY 2u
#define CHOS_FILE_META_READ_ONLY 4u
#define CHOS_FILE_META_ID_VALID 8u
#define CHOS_FILE_META_DIRTY 16u
#define CHOS_FILE_META_WORKING_COPY 32u
#define CHOS_FILE_META_SIZE_VALID 64u
/* Read-only runtime identity, never a user capability or POSIX inode/dev pair.
 * CHFS values come from the actual mount/object ledger; ROMFS uses its stable
 * compiled namespace ordinal. Source tags distinguish these identity domains.
 * Unidentified virtual directories explicitly omit ID_VALID. */
typedef struct chos_file_identity {
 uint32_t source,reserved;
 uint64_t mount,object;
#ifdef __cplusplus
 constexpr bool operator==(const chos_file_identity& other) const {
  return source==other.source&&reserved==other.reserved&&mount==other.mount&&object==other.object;
 }
 constexpr bool operator!=(const chos_file_identity& other) const {return !(*this==other);}
#endif
} chos_file_identity_t;
typedef struct {
 uint32_t version,size_bytes,flags,reserved0;
 chos_file_identity_t identity;
 uint64_t length,source_revision,reserved1;
} chos_file_metadata_t;
_Static_assert(sizeof(chos_file_identity_t)==24u,"file identity ABI size");
_Static_assert(sizeof(chos_file_metadata_t)==64u,"file metadata ABI size");
#endif
