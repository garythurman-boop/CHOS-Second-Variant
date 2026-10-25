#include "chos/libc_string.h"
#include <stdint.h>
#ifndef CHOS_LIBC_NO_EXPORTS
#include <string.h>
#endif

void *chos_libc_memcpy(void *dest,const void *source,size_t n){
    unsigned char *d=dest;const unsigned char *s=source;for(size_t i=0;i<n;i++)d[i]=s[i];return dest;
}
void *chos_libc_memmove(void *dest,const void *source,size_t n){
    unsigned char *d=dest;const unsigned char *s=source;
    if((uintptr_t)d<(uintptr_t)s){for(size_t i=0;i<n;i++)d[i]=s[i];}
    else if(d!=s){while(n){n--;d[n]=s[n];}}
    return dest;
}
void *chos_libc_memset(void *dest,int c,size_t n){
    unsigned char *d=dest;for(size_t i=0;i<n;i++)d[i]=(unsigned char)c;return dest;
}
int chos_libc_memcmp(const void *a,const void *b,size_t n){
    const unsigned char *x=a,*y=b;for(size_t i=0;i<n;i++)if(x[i]!=y[i])return (int)x[i]-(int)y[i];return 0;
}
void *chos_libc_memchr(const void *s,int c,size_t n){
    const unsigned char *p=s;for(size_t i=0;i<n;i++)if(p[i]==(unsigned char)c)return (void *)(p+i);return 0;
}
size_t chos_libc_strlen(const char *s){size_t n=0;while(s[n])n++;return n;}
size_t chos_libc_strnlen(const char *s,size_t max){size_t n=0;while(n<max&&s[n])n++;return n;}
int chos_libc_strcmp(const char *a,const char *b){
    while(*a && *a==*b){a++;b++;}return (int)(unsigned char)*a-(int)(unsigned char)*b;
}
int chos_libc_strncmp(const char *a,const char *b,size_t n){
    for(size_t i=0;i<n;i++){unsigned char x=(unsigned char)a[i],y=(unsigned char)b[i];if(x!=y)return (int)x-(int)y;if(!x)return 0;}return 0;
}
char *chos_libc_strcpy(char *d,const char *s){size_t i=0;do{d[i]=s[i];}while(s[i++]);return d;}
char *chos_libc_strncpy(char *d,const char *s,size_t n){size_t i=0;while(i<n&&s[i]){d[i]=s[i];i++;}while(i<n)d[i++]=0;return d;}
char *chos_libc_strcat(char *d,const char *s){chos_libc_strcpy(d+chos_libc_strlen(d),s);return d;}
char *chos_libc_strncat(char *d,const char *s,size_t n){size_t off=chos_libc_strlen(d),i=0;while(i<n&&s[i]){d[off+i]=s[i];i++;}d[off+i]=0;return d;}
char *chos_libc_strchr(const char *s,int c){do{if((unsigned char)*s==(unsigned char)c)return (char *)s;}while(*s++);return 0;}
char *chos_libc_strrchr(const char *s,int c){const char *last=0;do{if((unsigned char)*s==(unsigned char)c)last=s;}while(*s++);return (char *)last;}
char *chos_libc_strstr(const char *s,const char *needle){
    if(!*needle)return (char *)s;
    for(;*s;s++){size_t i=0;while(needle[i]&&s[i]&&s[i]==needle[i])i++;if(!needle[i])return (char *)s;}return 0;
}
#ifndef CHOS_LIBC_NO_EXPORTS
void *memcpy(void *d,const void *s,size_t n){return chos_libc_memcpy(d,s,n);}
void *memmove(void *d,const void *s,size_t n){return chos_libc_memmove(d,s,n);}
void *memset(void *d,int c,size_t n){return chos_libc_memset(d,c,n);}
int memcmp(const void *a,const void *b,size_t n){return chos_libc_memcmp(a,b,n);}
void *memchr(const void *s,int c,size_t n){return chos_libc_memchr(s,c,n);}
size_t strlen(const char *s){return chos_libc_strlen(s);}
size_t strnlen(const char *s,size_t n){return chos_libc_strnlen(s,n);}
int strcmp(const char *a,const char *b){return chos_libc_strcmp(a,b);}
int strncmp(const char *a,const char *b,size_t n){return chos_libc_strncmp(a,b,n);}
char *strcpy(char *d,const char *s){return chos_libc_strcpy(d,s);}
char *strncpy(char *d,const char *s,size_t n){return chos_libc_strncpy(d,s,n);}
char *strcat(char *d,const char *s){return chos_libc_strcat(d,s);}
char *strncat(char *d,const char *s,size_t n){return chos_libc_strncat(d,s,n);}
char *strchr(const char *s,int c){return chos_libc_strchr(s,c);}
char *strrchr(const char *s,int c){return chos_libc_strrchr(s,c);}
char *strstr(const char *s,const char *n){return chos_libc_strstr(s,n);}
#endif
