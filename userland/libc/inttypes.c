#include <inttypes.h>
#include <stdlib.h>
#include <limits.h>
_Static_assert(sizeof(intmax_t)==sizeof(long long)&&INTMAX_MAX==LLONG_MAX,"intmax conversion ABI");
_Static_assert(sizeof(uintmax_t)==sizeof(unsigned long long)&&UINTMAX_MAX==ULLONG_MAX,"uintmax conversion ABI");
intmax_t imaxabs(intmax_t n){return n<0?-n:n;}
imaxdiv_t imaxdiv(intmax_t n,intmax_t d){return (imaxdiv_t){n/d,n%d};}
intmax_t strtoimax(const char *s,char **end,int base){return (intmax_t)strtoll(s,end,base);}
uintmax_t strtoumax(const char *s,char **end,int base){return (uintmax_t)strtoull(s,end,base);}
