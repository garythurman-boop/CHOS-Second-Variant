#ifndef CHOS_LIBC_ARPA_INET_H
#define CHOS_LIBC_ARPA_INET_H

#include <netinet/in.h>

typedef uint32_t socklen_t;
#define INET_ADDRSTRLEN 16
#define INET6_ADDRSTRLEN 46

#ifdef __cplusplus
extern "C" {
#endif
int inet_pton(int, const char *, void *);
const char *inet_ntop(int, const void *, char *, socklen_t);
#ifdef __cplusplus
}
#endif
#endif
