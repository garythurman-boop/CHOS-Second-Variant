#include <ctype.h>
int isalpha(int c){return (unsigned)(c|32)-'a'<26u;}
int isdigit(int c){return (unsigned)c-'0'<10u;}
int isalnum(int c){return isalpha(c)||isdigit(c);}
int isblank(int c){return c==' '||c=='\t';}
int iscntrl(int c){return (unsigned)c<32u||c==127;}
int isgraph(int c){return (unsigned)c-33u<94u;}
int islower(int c){return (unsigned)c-'a'<26u;}
int isprint(int c){return (unsigned)c-32u<95u;}
int ispunct(int c){return isgraph(c)&&!isalnum(c);}
int isspace(int c){return c==' '||(unsigned)c-'\t'<5u;}
int isupper(int c){return (unsigned)c-'A'<26u;}
int isxdigit(int c){return isdigit(c)||(unsigned)(c|32)-'a'<6u;}
int tolower(int c){return isupper(c)?c+32:c;}
int toupper(int c){return islower(c)?c-32:c;}
int isascii(int c){return (unsigned)c<128u;}
int toascii(int c){return c&127;}
