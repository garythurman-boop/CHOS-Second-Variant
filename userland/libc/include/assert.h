/* Native C/C++ assertion contract. Re-inclusion intentionally follows NDEBUG. */
#undef assert
#ifndef CHOS_ASSERT_DECLARED
#define CHOS_ASSERT_DECLARED
#ifdef __cplusplus
extern "C" {
#endif
void __assert_fail(const char *,const char *,unsigned,const char *) __attribute__((noreturn));
#ifdef __cplusplus
}
#endif
#endif
#ifdef NDEBUG
#define assert(expression) ((void)0)
#else
#define assert(expression) ((expression)?(void)0:__assert_fail(#expression,__FILE__,__LINE__,__func__))
#endif
