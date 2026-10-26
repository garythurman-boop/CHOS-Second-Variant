#ifndef CHOS_CRYPTO_ASSERT_H
#define CHOS_CRYPTO_ASSERT_H
#define assert(c) ((c)?(void)0:__builtin_trap())
#endif
