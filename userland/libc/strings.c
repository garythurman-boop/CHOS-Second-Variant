#include <strings.h>
#include <ctype.h>
static int compare_byte(unsigned char a,unsigned char b){return tolower(a)-tolower(b);}
int strcasecmp(const char *left,const char *right){
    const unsigned char *a=(const unsigned char *)left,*b=(const unsigned char *)right;
    for(;;++a,++b){int difference=compare_byte(*a,*b);if(difference||!*a||!*b)return difference;}
}
int strncasecmp(const char *left,const char *right,size_t count){
    const unsigned char *a=(const unsigned char *)left,*b=(const unsigned char *)right;
    for(size_t i=0;i<count;i++){int difference=compare_byte(a[i],b[i]);if(difference||!a[i]||!b[i])return difference;}
    return 0;
}
