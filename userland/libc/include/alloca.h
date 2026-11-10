#ifndef CHOS_LIBC_ALLOCA_H
#define CHOS_LIBC_ALLOCA_H
#include <stddef.h>
/* Compiler extension: storage belongs to the current function's stack frame.
 * It is not heap storage, cannot be freed and remains subject to stack guards.
 */
#if defined(__GNUC__) || defined(__clang__)
#define alloca(length) __builtin_alloca(length)
#else
#error CHOS alloca requires a compiler with native stack-allocation support
#endif
#endif
