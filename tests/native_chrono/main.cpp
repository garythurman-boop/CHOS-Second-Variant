#include <chrono>
#include <list>
#include <forward_list>
#include <thread>
#include <atomic>
#include <iterator>
#include <cstdio>
#include <cstring>
#include <chos/user.h>
#include <chos/libc.h>
#include "calendar_vectors.h"
using namespace std::chrono;
using namespace std::chrono_literals;
static bool fail(unsigned n){printf("[FAIL] native chrono/list line %u\n",n);return false;}
#define CHECK(v) do{if(!(v))return fail(__LINE__);}while(0)
static_assert(year{2000}.is_leap()&&!year{1900}.is_leap()&&year{0}.is_leap()&&year{-400}.is_leap()&&!year{-100}.is_leap());
static_assert(duration_cast<milliseconds>(seconds{3})==3000ms&&floor<seconds>(-1500ms)==-2s);
static_assert(sys_days{year{1970}/January/1}.time_since_epoch()==days{0});
static bool calendars(){
 for(auto v:calendar_vectors){
  sys_days day{days{v.days}};year_month_day value{day};
  CHECK(value.ok()&&int(value.year())==v.year&&unsigned(value.month())==unsigned(v.month)&&unsigned(value.day())==unsigned(v.day));
  CHECK(sys_days{value}==day&&weekday{day}.c_encoding()==unsigned(v.weekday));
  CHECK((day-sys_days{value.year()/January/1}).count()==v.yearday-1);
  year_month_day_last end{value.year(),month_day_last{value.month()}};CHECK(unsigned(end.day())==unsigned(v.monthdays));
  year_month_weekday nth{day};CHECK(nth.index()==unsigned((v.day-1)/7+1)&&sys_days{nth}==day);
 }
 CHECK(!(year{1900}/February/29).ok()&&(year{2000}/February/29).ok());return true;
}
static bool durations(){
 CHECK(ceil<seconds>(-1500ms)==-1s&&round<seconds>(-2500ms)==-2s&&round<seconds>(-3500ms)==-4s);
 CHECK(round<seconds>(duration<double>{2.5})==2s&&round<seconds>(duration<double>{3.5})==4s);
 CHECK(duration_cast<seconds>(duration<long long,std::ratio<2,3>>{21})==14s);
 hh_mm_ss h{-(25h+4min+3s+456ms)};CHECK(h.is_negative()&&h.hours()==25h&&h.minutes()==4min&&h.seconds()==3s&&h.subseconds()==456ms);
 CHECK(h.to_duration()==-(25h+4min+3s+456ms));
 for(time_t t:{time_t(-2208988800LL),time_t(-1),time_t(0),time_t(2147483647),time_t(4102444800LL)})CHECK(system_clock::to_time_t(system_clock::from_time_t(t))==t);
 timespec before,after;CHECK(!clock_gettime(CLOCK_REALTIME,&before));auto file=file_clock::now();CHECK(!clock_gettime(CLOCK_REALTIME,&after));
 __int128 lower=__int128(before.tv_sec)*1000000000+before.tv_nsec,upper=__int128(after.tv_sec)*1000000000+after.tv_nsec;
 CHECK(file.time_since_epoch().count()>=lower&&file.time_since_epoch().count()<=upper);
 auto sys=system_clock::from_time_t(4102444800LL);CHECK(file_clock::to_sys(file_clock::from_sys(sys))==sys);
 auto start=steady_clock::now();auto wall=system_clock::now();std::this_thread::sleep_for(30ms);
 CHECK(steady_clock::now()-start>=30ms&&system_clock::now()>=wall&&steady_clock::is_steady&&!system_clock::is_steady);return true;
}
static bool containers(){
 std::list<int> a{7,1,3,1},b{8,0,6};int *held=&a.front();a.sort();b.sort();a.merge(b);
 CHECK(b.empty()&&a.size()==7);int i=0;for(int n:a){constexpr int expected[]={0,1,1,3,6,7,8};CHECK(n==expected[i++]);}
 a.unique();a.reverse();CHECK(a.size()==6&&*held==7);
 std::list<int> c{42};c.splice(c.end(),a,a.begin(),std::next(a.begin(),2));CHECK(c.size()==3&&a.size()==4&& &c.back()==held);
 auto copy=c;auto moved=std::move(copy);CHECK(copy.empty()&&moved==c);moved.clear();CHECK(moved.empty());
 std::forward_list<int> f{5,1,3},g{4,0,2};f.sort();g.sort();f.merge(g);CHECK(g.empty()&&std::distance(f.begin(),f.end())==6);
 i=0;for(int n:f)CHECK(n==i++);g.splice_after(g.before_begin(),f);CHECK(f.empty()&&g.front()==0);return true;
}
static std::atomic<unsigned> errors;
static bool worker(unsigned seed){
 CHECK(calendars());std::list<unsigned> values;uint64_t expected=0,observed=0;
 for(unsigned i=0;i<512;i++){unsigned v=(i*137+seed)%257;values.push_back(v);expected+=v;}
 values.sort();unsigned last=0;for(unsigned v:values){CHECK(v>=last);last=v;observed+=v;}CHECK(observed==expected);return true;
}
static bool run(){
 CHECK(calendars()&&durations()&&containers());
 for(unsigned wave=0;wave<3;wave++){
  {std::thread a([]{if(!worker(1))++errors;}),b([]{if(!worker(2))++errors;}),c([]{if(!worker(3))++errors;});a.join();b.join();c.join();}
  CHECK(!errors);chos_heap_stats_t heap;CHECK(!chos_libc_heap_info(&heap)&&!heap.allocations&&!heap.mapped_bytes);
 }
 for(const char *mode:{"deny-map","deny-clock"}){
  unsigned denied=!strcmp(mode,"deny-map")?CHOS_SYS_MAP_MEMORY:CHOS_SYS_CLOCK_INFO;
  long pid=chos_spawn_limited("/CHRONO.ELF",mode,CHOS_PROCESS_CAP_ALL&~CHOS_PROCESS_CAP(denied));CHECK(pid>0);
  CHECK(chos_wait((uint32_t)pid)==134&&chos_getstatus(pid)==CHOS_STATUS_EXITED);
 }
 chos_heap_stats_t heap;CHECK(!chos_libc_heap_info(&heap)&&!heap.allocations&&!heap.mapped_bytes);
 puts("[ OK ] original native chrono calendars,clocks and linked containers passed");return true;
}
int main(int argc,char **argv){
 if(argc>1&&!strcmp(argv[1],"deny-clock")){
  puts("[ OK ] entering denied native file clock query");volatile auto value=file_clock::now().time_since_epoch().count();(void)value;
  puts("[FAIL] denied clock query succeeded");return 3;
 }
 if(argc>1&&!strcmp(argv[1],"deny-map")){
  puts("[ OK ] entering denied native container allocation");std::list<int> a;a.push_back(1);puts("[FAIL] denied allocation succeeded");return 2;
 }
 return run()?0:1;
}
