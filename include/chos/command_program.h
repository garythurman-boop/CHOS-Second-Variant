#ifndef CHOS_COMMAND_PROGRAM_H
#define CHOS_COMMAND_PROGRAM_H
#include <stddef.h>
/* Split one unquoted program path and its space/tab-separated argv tail.
 * Filesystem paths cannot contain whitespace. Output path uses a distinct
 * buffer;empty/invalid/oversized input leaves both outputs unchanged.
 * Returns1 on success,0 for empty input,-1 for invalid/oversized output. */
static inline int chos_command_program(const char *text,char *path,size_t capacity,const char **arguments) {
    if(!text||!path||!arguments||capacity<2u)return -1;
    while(*text==' '||*text=='\t')++text;
    size_t length=0;
    while(text[length]&&text[length]!=' '&&text[length]!='\t') {
        if(length>=capacity-1u)return -1;
        ++length;
    }
    if(!length)return 0;
    const char *tail=text+length;
    while(*tail==' '||*tail=='\t')++tail;
    for(size_t i=0;i<length;++i)path[i]=text[i];
    path[length]='\0';*arguments=tail;return 1;
}
#endif
