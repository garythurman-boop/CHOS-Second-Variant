#!/usr/bin/env python3
"""Independent unbounded Python/calendar normalization and civil-date corpus."""
import argparse,calendar,json,pathlib,random
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--check',action='store_true');o=p.parse_args()
MIN_YEAR=-(1<<31)+1900;MAX_YEAR=(1<<31)-1+1900

def start(year):return 365*(year-1970)+calendar.leapdays(1970,year)
def month_days(year,month):return calendar.mdays[month]+(month==2 and calendar.isleap(year))
def normalize(fields):
 sec,minute,hour,day,month,year,*_=fields
 adjustment,month=divmod(month,12);year+=1900+adjustment
 days=start(year)+sum(month_days(year,m) for m in range(1,month+1))+day-1
 total=days*86400+hour*3600+minute*60+sec;days,within=divmod(total,86400)
 if not start(MIN_YEAR)<=days<start(MAX_YEAR+1):return 0,[0]*9,1
 lo,hi=MIN_YEAR,MAX_YEAR+1
 while lo+1<hi:
  mid=(lo+hi)//2
  if start(mid)<=days:lo=mid
  else:hi=mid
 year=lo;yday=days-start(year);remaining=yday;month=1
 while remaining>=month_days(year,month):remaining-=month_days(year,month);month+=1
 hour,within=divmod(within,3600);minute,sec=divmod(within,60)
 return total,[sec,minute,hour,remaining+1,month-1,year-1900,(days+4)%7,yday,0],0

rows=[];years=[-999999,-10000,-1,0,1,4,100,400,1600,1700,1900,1969,1970,2000,2024,2100,2400,10000,275760,999999,1000000000]
for year in years:
 for month in range(1,13):
  for day in [1,month_days(year,month)]:
   rows.append([59,59,23,day,month-1,year-1900,97,-99,-1])
for field in range(6):
 for value in [-(1<<31),-(1<<31)+1,-100000,-400,-1,0,1,29,60,100,8099,(1<<31)-2,(1<<31)-1]:
  fields=[0,0,0,1,0,100,0,0,0];fields[field]=value;rows.append(fields)
rng=random.Random(0x43414c454e444152)
for i in range(120):rows.append([rng.randrange(-(1<<31),1<<31) for _ in range(8)]+[i%3-1])
s='/* Independent unbounded Python integer/calendar oracle. */\n'
s+='struct calendar_vector {int fields[9];int64_t seconds;int normalized[9];bool error;};\nstatic const calendar_vector calendar_vectors[]={\n'
for fields in rows:
 seconds,normalized,error=normalize(fields)
 s+='{{'+','.join(map(str,fields))+'},INT64_C('+str(seconds)+'),{'+','.join(map(str,normalized))+'},'+('true' if error else 'false')+'},\n'
s+='};\n';target=pathlib.Path(__file__).with_name('calendar_vectors.h')
if o.check:assert target.read_text()==s,'calendar reference corpus changed'
else:target.write_text(s)
print(len(rows),'independent full-width UTC calendar vectors')
