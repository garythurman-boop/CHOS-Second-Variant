#include <stdlib.h>
#include <string.h>
#include "chos/user.h"
int main(int argc,char **argv){
 if(argc!=3||strcmp(argv[1],"alpha")||strcmp(argv[2],"beta"))return 1;
 char *p=calloc(32,1);if(!p)return 2;for(unsigned i=0;i<32;i++)if(p[i])return 3;free(p);
 const char *s="[ OK ] native SDK C main arguments and allocation passed\n";chos_write(s,strlen(s));return 19;
}
