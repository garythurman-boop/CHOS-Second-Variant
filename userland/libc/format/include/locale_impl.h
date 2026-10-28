#ifndef CHOS_PRIVATE_FORMAT_LOCALE_H
#define CHOS_PRIVATE_FORMAT_LOCALE_H
#include "stdio_impl.h"
/* This internal error-message path implements the runtime's invariant C locale.
 * It does not export a locale_t ABI or pretend to translate messages. */
#define locale_t __chos_format_locale_t
typedef void *locale_t;
#define CURRENT_LOCALE ((locale_t)0)
#define LC_MESSAGES 5
#define LCTRANS(s,category,locale) ((void)(locale),(s))
#define weak_alias(original,alias)
#endif
