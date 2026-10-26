#ifndef CHOS_TLS_STDIO_H
#define CHOS_TLS_STDIO_H
/* Library format/reporting functions are discarded from the client link.
 * No filesystem or standard-stream implementation is advertised here. */
#ifdef CHOS_TLS_HOST_TEST
#include_next <stdio.h>
#else
#include <stddef.h>
int snprintf(char *,size_t,const char *,...);
#endif
#endif
