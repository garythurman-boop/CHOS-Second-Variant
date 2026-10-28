/* Per-address-space environment and invariant C locale metadata. The native
 * spawn ABI currently passes argv only; every new process starts empty. */
#include <stdlib.h>
#include <locale.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
static char *empty_environment[]={NULL};
char **environ=empty_environment;
char *getenv(const char *name) {
    if (!name || !*name || strchr(name,'=')) return NULL;
    size_t n=strlen(name);
    for (char **entry=environ;entry && *entry;entry++)
        if (!strncmp(*entry,name,n) && (*entry)[n]=='=') return *entry+n+1;
    return NULL;
}
static const char *const categories[]={"LC_CTYPE","LC_NUMERIC","LC_TIME","LC_COLLATE","LC_MONETARY","LC_MESSAGES"};
static const char *environment_locale(int category) {
    char *value=getenv("LC_ALL"); if (value && *value) return value;
    value=getenv(categories[category]); if (value && *value) return value;
    value=getenv("LANG"); return value && *value ? value : "C";
}
static int supported(const char *name) { return !strcmp(name,"C") || !strcmp(name,"POSIX"); }
char *setlocale(int category,const char *name) {
    static char invariant[]="C";
    if (category<LC_CTYPE || category>LC_ALL) { errno=EINVAL; return NULL; }
    if (!name) return invariant;
    if (!*name) {
        for (int i=0;i<LC_ALL;i++)
            if ((category==LC_ALL || category==i) && !supported(environment_locale(i))) {
                errno=ENOENT; return NULL;
            }
    } else if (!supported(name)) { errno=ENOENT; return NULL; }
    return invariant;
}
struct lconv *localeconv(void) {
    static char empty[]="",decimal[]=".";
    static struct lconv value={
        .decimal_point=decimal,.thousands_sep=empty,.grouping=empty,.int_curr_symbol=empty,
        .currency_symbol=empty,.mon_decimal_point=empty,.mon_thousands_sep=empty,.mon_grouping=empty,
        .positive_sign=empty,.negative_sign=empty,
        .int_frac_digits=CHAR_MAX,.frac_digits=CHAR_MAX,.p_cs_precedes=CHAR_MAX,.p_sep_by_space=CHAR_MAX,
        .n_cs_precedes=CHAR_MAX,.n_sep_by_space=CHAR_MAX,.p_sign_posn=CHAR_MAX,.n_sign_posn=CHAR_MAX,
        .int_p_cs_precedes=CHAR_MAX,.int_p_sep_by_space=CHAR_MAX,.int_n_cs_precedes=CHAR_MAX,
        .int_n_sep_by_space=CHAR_MAX,.int_p_sign_posn=CHAR_MAX,.int_n_sign_posn=CHAR_MAX
    };
    return &value;
}
/* Calendar conversion in this native profile is UTC. Desktop time-zone display
 * settings and a future IANA-zone service are separate; TZ does not alter this. */
static char utc[]="UTC";
char *tzname[2]={utc,utc};
