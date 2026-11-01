#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cerrno>
#include <cwchar>
#include <cstdlib>
#include <queue>
#include <deque>
#include <memory>
#include <functional>
#include <thread>
#include <atomic>
#include <chos/user.h>
#include <chos/libc.h>
static bool fail(unsigned n){printf("[FAIL] native scanf/queue line %u errno %d\n",n,errno);return false;}
#define CHECK(v) do{if(!(v))return fail(__LINE__);}while(0)
static int scan(const char *s,const char *f,...){va_list a;va_start(a,f);int r=vsscanf(s,f,a);va_end(a);return r;}
static bool scanning(){
 int i=99,n=-1;unsigned u=0;long long ll=0;unsigned long long ull=0;
 CHECK(scan(" -42 0xfeed -9223372036854775808 18446744073709551615!","%d %x %lld %llu%n",&i,&u,&ll,&ull,&n)==4);
 CHECK(i==-42&&u==0xfeed&&ll==INT64_MIN&&ull==UINT64_MAX&&n==53);
 i=99;CHECK(scan("","%d",&i)==EOF&&i==99&&scan("x","%d",&i)==0&&i==99);
 CHECK(scan("","%n",&n)==0&&n==0);errno=0;CHECK(scan("1","%2147483648d",&i)==EOF&&i==99&&errno==EOVERFLOW);
 errno=0;CHECK(scan("1","%0$d",&i)==EOF&&errno==EINVAL&&i==99);
 char word[8]={},chars[4]={};CHECK(scan("abc123 ZQ","%3[a-z]%*3d %2c%n",word,chars,&n)==2&&!strcmp(word,"abc")&&!memcmp(chars,"ZQ",2)&&n==9);
 char *text=nullptr;CHECK(scan("hello world","%ms",&text)==1&&text&&!strcmp(text,"hello"));free(text);
 float f=0;double d=0;long double q=0;
 CHECK(scan("0x1p-149 0x1p-1074 0x1.0000000000000000000000000001p0","%a %la %La",&f,&d,&q)==3&&f==0x1p-149f&&d==0x1p-1074);
 struct {uint64_t lo,hi;} bits;static_assert(sizeof(q)==16);memcpy(&bits,&q,16);CHECK(bits.lo==1&&bits.hi==UINT64_C(0x3fff000000000000));
 CHECK(scan("0x1p-16494","%La",&q)==1);memcpy(&bits,&q,16);CHECK(bits.lo==1&&!bits.hi);
 wchar_t a=0,b=0;CHECK(scan("\xc3\xa9X","%lc%n%lc",&a,&n,&b)==2&&a==0xe9&&b=='X'&&n==2);
 wchar_t wide[8]={};CHECK(scan("\xc3\xa9\xf0\x9f\x99\x82Z","%2lc%n%lc",wide,&n,&b)==2&&wide[0]==0xe9&&wide[1]==0x1f642&&b=='Z'&&n==6);
 CHECK(scan("\xc3\xa9X","%*1lc%lc",&b)==1&&b=='X');
 a=77;errno=0;CHECK(scan("\xc3","%lc",&a)==EOF&&a==77&&errno==EILSEQ);
 errno=0;CHECK(scan("\xc3\xa9","%l[\xc3\xa9]",wide)==EOF&&errno==ENOTSUP);
 char empty[1]={},c=42;CHECK(scan(empty,"%c",&c)==EOF&&c==42);return true;
}
static bool containers(unsigned salt){
 std::deque<unsigned> values;for(unsigned i=0;i<4096;i++)values.push_back(i+salt);
 auto *held=&values[2048];for(unsigned i=0;i<128;i++)values.push_front(5000+i);
 CHECK(&values[2176]==held&&*held==2048+salt);
 for(unsigned i=0;i<128;i++){CHECK(values.front()==5127-i);values.pop_front();}
 CHECK(&values[2048]==held);auto copied=values;auto moved=std::move(copied);CHECK(moved==values);
 for(unsigned i=0;i<4096;i++){CHECK(moved.front()==i+salt);moved.pop_front();}CHECK(moved.empty());
 std::queue<std::unique_ptr<unsigned>> fifo;for(unsigned i=0;i<512;i++)fifo.push(std::make_unique<unsigned>(i+salt));
 for(unsigned i=0;i<512;i++){CHECK(!fifo.empty()&&*fifo.front()==i+salt);fifo.pop();}CHECK(fifo.empty());
 std::priority_queue<unsigned,std::vector<unsigned>,std::greater<unsigned>> priority;unsigned counts[257]={};
 for(unsigned i=0;i<1024;i++){unsigned v=(i*137+salt)%257;priority.push(v);++counts[v];}
 for(unsigned i=0;i<257;i++)for(unsigned j=0;j<counts[i];j++){CHECK(!priority.empty()&&priority.top()==i);priority.pop();}
 CHECK(priority.empty());return true;
}
static std::atomic<unsigned> errors;
static bool worker(unsigned salt){
 CHECK(containers(salt));
 for(unsigned i=0;i<64;i++){
  char input[64];int value=(int)(i*7919+salt),result=0,count=-1;snprintf(input,sizeof input,"%d rest",value);
  CHECK(sscanf(input,"%d %n",&result,&count)==1&&result==value&&count>0);
  errno=0;CHECK(scan("1","%999999999999999999999d",&result)==EOF&&errno==EOVERFLOW);
  CHECK(!chos_yield()&&errno==EOVERFLOW);
 }
 return true;
}
static bool run(){
 CHECK(scanning()&&containers(1));
 for(unsigned wave=0;wave<3;wave++){
  {std::thread a([wave]{if(!worker(11+wave))++errors;}),b([wave]{if(!worker(101+wave))++errors;}),c([wave]{if(!worker(201+wave))++errors;});a.join();b.join();c.join();}
  CHECK(!errors);chos_heap_stats_t h;CHECK(!chos_libc_heap_info(&h)&&!h.allocations&&!h.mapped_bytes);
 }
 puts("[ OK ] native memory scanf,UTF8 wide fields and original deque/queue lifecycles passed");return true;
}
int main(){return run()?0:1;}
