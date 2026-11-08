#ifndef CHOS_LIBC_SETJMP_H
#define CHOS_LIBC_SETJMP_H
#if defined(CHOS_HOST_TEST)
#include_next <setjmp.h>
#else
#if !defined(__aarch64__) || __SIZEOF_LONG__ != 8
#error CHOS nonlocal jumps require the native AArch64 ABI
#endif
/* The original musl AArch64 register layout. This native interface has no
 * signal-mask state. A buffer may only target its still-live saving frame in
 * the same thread. Nonvolatile locals changed since setjmp are indeterminate.
 * Long jumps do not unwind C++ objects or run cleanup handlers.
 */
typedef unsigned long jmp_buf[22];
#ifdef __cplusplus
extern "C" {
#endif
int setjmp(jmp_buf) __attribute__((returns_twice));
void longjmp(jmp_buf,int) __attribute__((noreturn));
int _setjmp(jmp_buf) __attribute__((returns_twice));
void _longjmp(jmp_buf,int) __attribute__((noreturn));
#ifdef __cplusplus
}
#endif
#define setjmp setjmp
#endif
#endif
