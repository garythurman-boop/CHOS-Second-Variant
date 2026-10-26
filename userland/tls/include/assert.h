#ifndef CHOS_TLS_ASSERT_H
#define CHOS_TLS_ASSERT_H
void chos_tls_assert_failed(void) __attribute__((noreturn));
#define assert(condition) ((condition)?(void)0:chos_tls_assert_failed())
#endif
