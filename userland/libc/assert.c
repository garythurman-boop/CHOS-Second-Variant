#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "chos/user.h"
static void write_text(const char *s){if(s)chos_write(s,strlen(s));}
void __assert_fail(const char *expression,const char *file,unsigned line,const char *function){
 char number[10];unsigned n=0;
 do{number[n++]=(char)('0'+line%10);line/=10;}while(line);
 write_text("Assertion failed: ");write_text(expression);write_text(" at ");write_text(file);write_text(":");
 while(n)chos_write(&number[--n],1);
 write_text(" in ");write_text(function);write_text("\n");abort();
}
