#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>

static char *copy_string(const char *source,size_t length) {
    if(length==SIZE_MAX){errno=ENOMEM;return NULL;}
    char *copy=malloc(length+1);
    if(!copy)return NULL;
    memcpy(copy,source,length);copy[length]=0;
    return copy;
}
char *strdup(const char *source) {return copy_string(source,strlen(source));}
char *strndup(const char *source,size_t maximum) {return copy_string(source,strnlen(source,maximum));}
