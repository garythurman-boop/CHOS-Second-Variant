#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "chos/libc_string.h"
int main(void){
    unsigned char a[128],b[128],source[128];for(unsigned i=0;i<128;i++)source[i]=(unsigned char)(i*91u);
    for(unsigned n=0;n<=128;n++){
        memset(a,0,128);memset(b,0,128);chos_libc_memcpy(a,source,n);memcpy(b,source,n);assert(!memcmp(a,b,128));
        chos_libc_memset(a,0x189,n);memset(b,0x189,n);assert(!memcmp(a,b,128));
        assert(!chos_libc_memcmp(a,b,128));assert(chos_libc_memchr(source,source[17],n)==memchr(source,source[17],n));
    }
    for(unsigned dst=0;dst<32;dst++)for(unsigned src=0;src<32;src++)for(unsigned n=0;n<64;n++){
        memcpy(a,source,128);memcpy(b,source,128);chos_libc_memmove(a+dst,a+src,n);memmove(b+dst,b+src,n);assert(!memcmp(a,b,128));}
    char text[128];assert(chos_libc_strlen("CHOS")==4);assert(chos_libc_strnlen("abcdef",3)==3);
    assert(chos_libc_strnlen("abc",8)==3);assert(!chos_libc_strcmp("same","same"));
    assert(chos_libc_strcmp("\xff","\x01")>0);assert(!chos_libc_strncmp("helloX","helloY",5));
    assert(!chos_libc_strncmp("x","x",50));assert(chos_libc_strncmp("helloX","helloY",6)<0);
    assert(chos_libc_strcpy(text,"CHOS")==text);assert(!strcmp(text,"CHOS"));
    assert(chos_libc_strcat(text," libc")==text);assert(!strcmp(text,"CHOS libc"));
    chos_libc_strncat(text," string trailing",7);assert(!strcmp(text,"CHOS libc string"));
    assert(chos_libc_strchr(text,'i')==text+6);assert(chos_libc_strrchr(text,'i')==text+13);
    assert(chos_libc_strchr(text,0)==text+strlen(text));assert(chos_libc_strrchr(text,0)==text+strlen(text));
    assert(!chos_libc_strchr(text,'z'));assert(chos_libc_strstr(text,"libc")==text+5);
    assert(chos_libc_strstr(text,"")==text);assert(!chos_libc_strstr(text,"string-longer"));
    memset(text,0x51,sizeof(text));chos_libc_strncpy(text,"abc",7);assert(!memcmp(text,"abc\0\0\0\0Q",8));
    chos_libc_strncpy(text,"abcdef",3);assert(!memcmp(text,"abc",3));
    unsigned char c[]={0xff},d[]={0};assert(chos_libc_memcmp(c,d,1)>0);
    puts("libc strings: PASS (overlap, bounded operations, unsigned bytes, strings and terminators)");return 0;
}
