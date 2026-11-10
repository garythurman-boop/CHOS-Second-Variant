/* Rename native exports so they cannot interpose sanitizer/host libc calls. */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#undef malloc
extern void *malloc(size_t);
static int saved_errno,deny;
static size_t requested;
int *__chos_errno_location(void){return &saved_errno;}
void *chos_test_malloc(size_t length){
    requested=length;
    if(deny){errno=ENOMEM;return NULL;}
    return malloc(length);
}
int main(void){
    char input[]={'a','b',0,'c','d',0};
    for(size_t limit=0;limit<=sizeof(input);++limit){
        char *copy=strndup(input,limit);assert(copy&&copy!=input);
        size_t wanted=limit<2?limit:2;
        assert(requested==wanted+1&&strlen(copy)==wanted&&!memcmp(copy,input,wanted));
        if(wanted){copy[0]='z';assert(input[0]=='a');}free(copy);
    }
    char *copy=strdup(input);assert(copy&&requested==3&&!strcmp(copy,"ab"));
    input[0]='x';assert(copy[0]=='a');free(copy);
    copy=strndup("",SIZE_MAX);assert(copy&&requested==1&&!*copy);free(copy);
    /* The input has exactly length bytes and no terminator. ASan catches a read
     * beyond that allocation; strndup must add its own output terminator. */
    for(size_t length=1;length<=8192;length*=2){
        char *source=malloc(length);assert(source);memset(source,'q',length);
        copy=strndup(source,length);assert(copy&&requested==length+1);
        assert(strlen(copy)==length&&!memcmp(copy,source,length));free(copy);free(source);
    }
    deny=1;errno=0;assert(!strdup("oom")&&errno==ENOMEM&&requested==4);
    errno=0;assert(!strndup("oom",2)&&errno==ENOMEM&&requested==3);
    errno=0;assert(!strndup("",0)&&errno==ENOMEM&&requested==1);
    puts("String allocation ownership, bounded reads, NUL termination and allocation failures passed");
    return 0;
}
