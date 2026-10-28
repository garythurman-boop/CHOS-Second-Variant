#ifndef CHOS_ICU_NATIVE_CONFIG_H
#define CHOS_ICU_NATIVE_CONFIG_H
/* A real static custom-OS port: file snapshots rather than mmap/dlopen. */
#define U_STATIC_IMPLEMENTATION 1
#define U_HAVE_MMAP 0
#define U_ENABLE_DYLOAD 0
#define U_HAVE_NL_LANGINFO_CODESET 0
#define U_CHARSET_IS_UTF8 1
/* Native libc currently has a fixed UTC calendar. These do not advertise a
 * POSIX TZ parser, IANA zone service or desktop time-zone preference bridge. */
#define U_HAVE_TZSET 0
#define U_TIMEZONE 0
#endif
