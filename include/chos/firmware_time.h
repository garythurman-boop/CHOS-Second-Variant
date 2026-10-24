#ifndef CHOS_FIRMWARE_TIME_H
#define CHOS_FIRMWARE_TIME_H
#include <stdint.h>
/* EFI_TIME layout. Conversion is shared with the loader, without runtime EFI calls. */
typedef struct {
    uint16_t year;
    uint8_t month,day,hour,minute,second,pad;
    uint32_t nanosecond;
    int16_t timezone;
    uint8_t daylight,pad2;
} chos_firmware_time_t;
static inline int chos_time_leap(unsigned y) {return y%4==0 && (y%100!=0 || y%400==0);}
static inline int chos_firmware_time_epoch(const chos_firmware_time_t *t,uint64_t *epoch) {
    static const uint8_t months[]={31,28,31,30,31,30,31,31,30,31,30,31};
    if(!t || !epoch || t->year<1970 || t->year>9999 || !t->month || t->month>12 ||
       !t->day || t->day>months[t->month-1]+(t->month==2 && chos_time_leap(t->year)) ||
       t->hour>23 || t->minute>59 || t->second>59 || t->nanosecond>=1000000000u ||
       (t->timezone!=2047 && (t->timezone< -1440 || t->timezone>1440)) || (t->daylight&~3u))return 0;
    uint64_t days=0;
    for(unsigned year=1970;year<t->year;++year)days+=365u+chos_time_leap(year);
    for(unsigned month=1;month<t->month;++month)days+=months[month-1]+(month==2 && chos_time_leap(t->year));
    int64_t seconds=(int64_t)((days+t->day-1)*86400u)+t->hour*3600u+t->minute*60u+t->second;
    /* An unspecified EFI timezone is retained as firmware wall time, interpreted
     * as UTC; the GUI's configured display offset is applied separately. */
    if(t->timezone!=2047)seconds-=(int64_t)t->timezone*60;
    if(seconds<0)return 0;
    *epoch=(uint64_t)seconds;return 1;
}
#endif
