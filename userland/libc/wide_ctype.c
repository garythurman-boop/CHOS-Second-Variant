/* Invariant C-locale wide classification/case conversion, not Unicode UCD.
 * UTF8 encoding is handled separately; ICU owns full Unicode properties. */
#include <wctype.h>
#include <ctype.h>
#include <string.h>
#define CLASS(name) int isw##name(wint_t c){return (uint32_t)c<128u?is##name((int)c):0;}
CLASS(alnum) CLASS(alpha) CLASS(blank) CLASS(cntrl) CLASS(digit) CLASS(graph)
CLASS(lower) CLASS(print) CLASS(punct) CLASS(space) CLASS(upper) CLASS(xdigit)
wint_t towlower(wint_t c){return (uint32_t)c<128u?(wint_t)tolower((int)c):c;}
wint_t towupper(wint_t c){return (uint32_t)c<128u?(wint_t)toupper((int)c):c;}
wctype_t wctype(const char *s){
 static const char *const names[]={"alnum","alpha","blank","cntrl","digit","graph","lower","print","punct","space","upper","xdigit"};
 for(unsigned i=0;i<sizeof(names)/sizeof(*names);i++)if(!strcmp(s,names[i]))return i+1;return 0;
}
int iswctype(wint_t c,wctype_t type){switch(type){
 case 1:return iswalnum(c);case 2:return iswalpha(c);case 3:return iswblank(c);case 4:return iswcntrl(c);
 case 5:return iswdigit(c);case 6:return iswgraph(c);case 7:return iswlower(c);case 8:return iswprint(c);
 case 9:return iswpunct(c);case 10:return iswspace(c);case 11:return iswupper(c);case 12:return iswxdigit(c);default:return 0;}}
wctrans_t wctrans(const char *s){return !strcmp(s,"tolower")?1:!strcmp(s,"toupper")?2:0;}
wint_t towctrans(wint_t c,wctrans_t t){return t==1?towlower(c):t==2?towupper(c):c;}
