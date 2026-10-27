#ifndef CHOS_LIBC_INTTYPES_H
#define CHOS_LIBC_INTTYPES_H
#include <stdint.h>
#if __SIZEOF_LONG__ != 8
#error Native inttypes requires the qualified LP64 profile
#endif
#define PRId8 "d"
#define PRIi8 "i"
#define PRIo8 "o"
#define PRIu8 "u"
#define PRIx8 "x"
#define PRIX8 "X"
#define PRId16 "d"
#define PRIi16 "i"
#define PRIo16 "o"
#define PRIu16 "u"
#define PRIx16 "x"
#define PRIX16 "X"
#define PRId32 "d"
#define PRIi32 "i"
#define PRIo32 "o"
#define PRIu32 "u"
#define PRIx32 "x"
#define PRIX32 "X"
#define PRId64 "ld"
#define PRIi64 "li"
#define PRIo64 "lo"
#define PRIu64 "lu"
#define PRIx64 "lx"
#define PRIX64 "lX"
#define PRIdMAX "ld"
#define PRIiMAX "li"
#define PRIoMAX "lo"
#define PRIuMAX "lu"
#define PRIxMAX "lx"
#define PRIXMAX "lX"
#define PRIdPTR "ld"
#define PRIiPTR "li"
#define PRIoPTR "lo"
#define PRIuPTR "lu"
#define PRIxPTR "lx"
#define PRIXPTR "lX"
/* Fast integer typedefs may differ between compilers; use stdint limits. */
#if INT_FAST8_MAX == INT8_MAX
#define __CHOS_FAST8_PREFIX "hh"
#elif INT_FAST8_MAX == INT16_MAX
#define __CHOS_FAST8_PREFIX "h"
#elif INT_FAST8_MAX == INT32_MAX
#define __CHOS_FAST8_PREFIX ""
#elif INT_FAST8_MAX == INT64_MAX
#define __CHOS_FAST8_PREFIX "l"
#else
#error Unsupported fast integer representation
#endif
#if INT_FAST16_MAX == INT8_MAX
#define __CHOS_FAST16_PREFIX "hh"
#elif INT_FAST16_MAX == INT16_MAX
#define __CHOS_FAST16_PREFIX "h"
#elif INT_FAST16_MAX == INT32_MAX
#define __CHOS_FAST16_PREFIX ""
#elif INT_FAST16_MAX == INT64_MAX
#define __CHOS_FAST16_PREFIX "l"
#else
#error Unsupported fast integer representation
#endif
#if INT_FAST32_MAX == INT8_MAX
#define __CHOS_FAST32_PREFIX "hh"
#elif INT_FAST32_MAX == INT16_MAX
#define __CHOS_FAST32_PREFIX "h"
#elif INT_FAST32_MAX == INT32_MAX
#define __CHOS_FAST32_PREFIX ""
#elif INT_FAST32_MAX == INT64_MAX
#define __CHOS_FAST32_PREFIX "l"
#else
#error Unsupported fast integer representation
#endif
#if INT_FAST64_MAX == INT8_MAX
#define __CHOS_FAST64_PREFIX "hh"
#elif INT_FAST64_MAX == INT16_MAX
#define __CHOS_FAST64_PREFIX "h"
#elif INT_FAST64_MAX == INT32_MAX
#define __CHOS_FAST64_PREFIX ""
#elif INT_FAST64_MAX == INT64_MAX
#define __CHOS_FAST64_PREFIX "l"
#else
#error Unsupported fast integer representation
#endif
#define PRIdLEAST8 PRId8
#define PRIdFAST8 __CHOS_FAST8_PREFIX "d"
#define PRIiLEAST8 PRIi8
#define PRIiFAST8 __CHOS_FAST8_PREFIX "i"
#define PRIoLEAST8 PRIo8
#define PRIoFAST8 __CHOS_FAST8_PREFIX "o"
#define PRIxLEAST8 PRIx8
#define PRIxFAST8 __CHOS_FAST8_PREFIX "x"
#define PRIuLEAST8 PRIu8
#define PRIuFAST8 __CHOS_FAST8_PREFIX "u"
#define PRIXLEAST8 PRIX8
#define PRIXFAST8 __CHOS_FAST8_PREFIX "X"
#define PRIdLEAST16 PRId16
#define PRIdFAST16 __CHOS_FAST16_PREFIX "d"
#define PRIiLEAST16 PRIi16
#define PRIiFAST16 __CHOS_FAST16_PREFIX "i"
#define PRIoLEAST16 PRIo16
#define PRIoFAST16 __CHOS_FAST16_PREFIX "o"
#define PRIxLEAST16 PRIx16
#define PRIxFAST16 __CHOS_FAST16_PREFIX "x"
#define PRIuLEAST16 PRIu16
#define PRIuFAST16 __CHOS_FAST16_PREFIX "u"
#define PRIXLEAST16 PRIX16
#define PRIXFAST16 __CHOS_FAST16_PREFIX "X"
#define PRIdLEAST32 PRId32
#define PRIdFAST32 __CHOS_FAST32_PREFIX "d"
#define PRIiLEAST32 PRIi32
#define PRIiFAST32 __CHOS_FAST32_PREFIX "i"
#define PRIoLEAST32 PRIo32
#define PRIoFAST32 __CHOS_FAST32_PREFIX "o"
#define PRIxLEAST32 PRIx32
#define PRIxFAST32 __CHOS_FAST32_PREFIX "x"
#define PRIuLEAST32 PRIu32
#define PRIuFAST32 __CHOS_FAST32_PREFIX "u"
#define PRIXLEAST32 PRIX32
#define PRIXFAST32 __CHOS_FAST32_PREFIX "X"
#define PRIdLEAST64 PRId64
#define PRIdFAST64 __CHOS_FAST64_PREFIX "d"
#define PRIiLEAST64 PRIi64
#define PRIiFAST64 __CHOS_FAST64_PREFIX "i"
#define PRIoLEAST64 PRIo64
#define PRIoFAST64 __CHOS_FAST64_PREFIX "o"
#define PRIxLEAST64 PRIx64
#define PRIxFAST64 __CHOS_FAST64_PREFIX "x"
#define PRIuLEAST64 PRIu64
#define PRIuFAST64 __CHOS_FAST64_PREFIX "u"
#define PRIXLEAST64 PRIX64
#define PRIXFAST64 __CHOS_FAST64_PREFIX "X"
#define SCNd8 "hhd"
#define SCNdLEAST8 SCNd8
#define SCNdFAST8 __CHOS_FAST8_PREFIX "d"
#define SCNi8 "hhi"
#define SCNiLEAST8 SCNi8
#define SCNiFAST8 __CHOS_FAST8_PREFIX "i"
#define SCNo8 "hho"
#define SCNoLEAST8 SCNo8
#define SCNoFAST8 __CHOS_FAST8_PREFIX "o"
#define SCNx8 "hhx"
#define SCNxLEAST8 SCNx8
#define SCNxFAST8 __CHOS_FAST8_PREFIX "x"
#define SCNu8 "hhu"
#define SCNuLEAST8 SCNu8
#define SCNuFAST8 __CHOS_FAST8_PREFIX "u"
#define SCNd16 "hd"
#define SCNdLEAST16 SCNd16
#define SCNdFAST16 __CHOS_FAST16_PREFIX "d"
#define SCNi16 "hi"
#define SCNiLEAST16 SCNi16
#define SCNiFAST16 __CHOS_FAST16_PREFIX "i"
#define SCNo16 "ho"
#define SCNoLEAST16 SCNo16
#define SCNoFAST16 __CHOS_FAST16_PREFIX "o"
#define SCNx16 "hx"
#define SCNxLEAST16 SCNx16
#define SCNxFAST16 __CHOS_FAST16_PREFIX "x"
#define SCNu16 "hu"
#define SCNuLEAST16 SCNu16
#define SCNuFAST16 __CHOS_FAST16_PREFIX "u"
#define SCNd32 "d"
#define SCNdLEAST32 SCNd32
#define SCNdFAST32 __CHOS_FAST32_PREFIX "d"
#define SCNi32 "i"
#define SCNiLEAST32 SCNi32
#define SCNiFAST32 __CHOS_FAST32_PREFIX "i"
#define SCNo32 "o"
#define SCNoLEAST32 SCNo32
#define SCNoFAST32 __CHOS_FAST32_PREFIX "o"
#define SCNx32 "x"
#define SCNxLEAST32 SCNx32
#define SCNxFAST32 __CHOS_FAST32_PREFIX "x"
#define SCNu32 "u"
#define SCNuLEAST32 SCNu32
#define SCNuFAST32 __CHOS_FAST32_PREFIX "u"
#define SCNd64 "ld"
#define SCNdLEAST64 SCNd64
#define SCNdFAST64 __CHOS_FAST64_PREFIX "d"
#define SCNi64 "li"
#define SCNiLEAST64 SCNi64
#define SCNiFAST64 __CHOS_FAST64_PREFIX "i"
#define SCNo64 "lo"
#define SCNoLEAST64 SCNo64
#define SCNoFAST64 __CHOS_FAST64_PREFIX "o"
#define SCNx64 "lx"
#define SCNxLEAST64 SCNx64
#define SCNxFAST64 __CHOS_FAST64_PREFIX "x"
#define SCNu64 "lu"
#define SCNuLEAST64 SCNu64
#define SCNuFAST64 __CHOS_FAST64_PREFIX "u"
#define SCNdMAX "ld"
#define SCNiMAX "li"
#define SCNoMAX "lo"
#define SCNxMAX "lx"
#define SCNuMAX "lu"
#define SCNdPTR "ld"
#define SCNiPTR "li"
#define SCNoPTR "lo"
#define SCNxPTR "lx"
#define SCNuPTR "lu"
typedef struct {intmax_t quot,rem;} imaxdiv_t;
#ifdef __cplusplus
extern "C" {
#endif
intmax_t imaxabs(intmax_t);
imaxdiv_t imaxdiv(intmax_t,intmax_t);
intmax_t strtoimax(const char *,char **,int);
uintmax_t strtoumax(const char *,char **,int);
#ifdef __cplusplus
}
#endif
#endif
