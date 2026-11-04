#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "chos/command_program.h"
static void reject(const char *text,size_t capacity,int expected) {
    char path[16],before[16];memset(path,0xa5,sizeof(path));memcpy(before,path,sizeof(path));
    const char *arguments="unchanged";const char *old=arguments;
    assert(chos_command_program(text,path,capacity,&arguments)==expected);
    assert(!memcmp(path,before,sizeof(path))&&arguments==old);
}
int main(void) {
    const struct {const char *input,*path,*tail;} cases[]={
        {"/BIN/X.ELF","/BIN/X.ELF",""},
        {" \t/BIN/X.ELF \t alpha\tbeta ","/BIN/X.ELF","alpha\tbeta "},
        {"local.chx\t","local.chx",""},
        {"/BIN/X.ELF ;data","/BIN/X.ELF",";data"},
        {"abcdef","abcdef",""}
    };
    for(size_t i=0;i<sizeof(cases)/sizeof(cases[0]);++i){
        char path[32];const char *tail=NULL;
        assert(chos_command_program(cases[i].input,path,sizeof(path),&tail)==1);
        assert(!strcmp(path,cases[i].path)&&!strcmp(tail,cases[i].tail));
        assert(tail>=cases[i].input&&tail<=cases[i].input+strlen(cases[i].input));
    }
    reject(NULL,16,-1);reject("",16,0);reject(" \t ",16,0);
    reject("abcdef",0,-1);reject("abcdef",1,-1);reject("abcdef",6,-1);
    char exact[9];memset(exact,0x55,sizeof(exact));const char *tail;
    assert(chos_command_program("abcdef tail",exact,7,&tail)==1);
    assert(!strcmp(exact,"abcdef")&&!strcmp(tail,"tail")&&exact[7]==0x55&&exact[8]==0x55);
    assert(chos_command_program("x",NULL,8,&tail)==-1);
    assert(chos_command_program("x",exact,sizeof(exact),NULL)==-1);
    puts("program command: path/argv boundaries,whitespace,data-only tail,exact capacity,canaries and unchanged failure outputs passed");return 0;
}
