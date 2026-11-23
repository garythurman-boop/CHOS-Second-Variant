#include <atomic>
#include <bitset>
#include <stack>
#include <string>
#include <vector>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#ifdef __CHOS__
#include <chos/libc.h>
#endif

#define CHECK(x) do { if (!(x)) { printf("[FAIL] stdlib containers line %u: %s\n",__LINE__,#x); return 1; } } while (0)

template<size_t N> static int bits()
{
    std::bitset<N> original;
    unsigned char expected[N ? N : 1] = {0};
    for (size_t i=0;i<N;++i) {
        expected[i]=(i%7==0 || i%13==2);
        original.set(i,expected[i]);
    }
    size_t count=0;
    for(size_t i=0;i<N;++i){CHECK(original.test(i)==bool(expected[i]));count+=expected[i];}
    CHECK(original.size()==N && original.count()==count);
    CHECK(original.any()==(count!=0) && original.none()==(count==0));
    CHECK(original.all()==(count==N));
    auto text=original.to_string();CHECK(text.size()==N);
    for(size_t i=0;i<N;++i)CHECK(text[N-1-i]==(expected[i]?'1':'0'));
    std::bitset<N> parsed(text);CHECK(parsed==original);
    auto custom=original.to_string('a','b');
    std::bitset<N> custom_parsed(custom,0,custom.size(),'a','b');CHECK(custom_parsed==original);

    const size_t shifts[]={0,1,N?N-1:0,N,N+1,63,64,65,128,257};
    for(size_t shift:shifts) {
        auto left=original<<shift,right=original>>shift;
        auto assigned=original;assigned<<=shift;CHECK(assigned==left);
        assigned=original;assigned>>=shift;CHECK(assigned==right);
        for(size_t i=0;i<N;++i) {
            CHECK(left[i]==bool(i>=shift ? expected[i-shift] : 0));
            CHECK(right[i]==bool(shift<N && i<N-shift ? expected[i+shift] : 0));
        }
    }
    std::bitset<N> other;
    for(size_t i=0;i<N;++i)other.set(i,i%3==1);
    auto both=original&other,either=original|other,different=original^other,inverted=~original;
    for(size_t i=0;i<N;++i) {
        CHECK(both[i]==bool(expected[i] && i%3==1));
        CHECK(either[i]==bool(expected[i] || i%3==1));
        CHECK(different[i]==bool(bool(expected[i]) != (i%3==1)));
        CHECK(inverted[i]==!bool(expected[i]));
    }
    auto assigned=original;assigned&=other;CHECK(assigned==both);
    assigned=original;assigned|=other;CHECK(assigned==either);
    assigned=original;assigned^=other;CHECK(assigned==different);
    assigned.set();CHECK(assigned.all() && assigned.count()==N);
    assigned.flip();CHECK(assigned.none());assigned.reset();CHECK(assigned.none());
    if constexpr(N) {
        assigned=original;assigned[N-1].flip();CHECK(assigned[N-1]!=original[N-1]);
        assigned[0]=assigned[N-1];CHECK(assigned[0]==assigned[N-1]);
        assigned.reset(N-1);CHECK(!assigned[N-1]);
    }
    return 0;
}

static std::atomic<int> live{0},created{0},destroyed{0};
struct Value {
    int id;
    explicit Value(int value):id(value){++live;++created;}
    Value(const Value& value):id(value.id){++live;++created;}
    Value(Value&& value) noexcept:id(value.id){value.id=-1;++live;++created;}
    Value& operator=(const Value& value){id=value.id;return *this;}
    Value& operator=(Value&& value) noexcept{id=value.id;value.id=-1;return *this;}
    ~Value(){--live;++destroyed;}
};

static int stacks()
{
    {
        std::stack<Value> original;
        CHECK(original.empty() && !original.size());
        for(int i=0;i<1536;++i){original.emplace(i*17+3);CHECK(original.top().id==i*17+3);}
        auto copy=original;
        CHECK(copy.size()==original.size());
        auto moved=static_cast<std::stack<Value>&&>(copy);
        std::stack<Value> alternate;alternate.emplace(-9);
        moved.swap(alternate);CHECK(moved.size()==1 && moved.top().id==-9);moved.pop();
        for(int i=1535;i>=0;--i) {
            CHECK(original.top().id==i*17+3 && alternate.top().id==i*17+3);
            original.pop();alternate.pop();
        }
        CHECK(original.empty() && alternate.empty());
        std::stack<Value,std::vector<Value>> vector_stack;
        for(int i=0;i<1200;++i)vector_stack.emplace(i^0x5a5);
        Value extra(7654);vector_stack.push(extra);CHECK(vector_stack.top().id==7654);vector_stack.pop();
        vector_stack.push(Value(9182));CHECK(vector_stack.top().id==9182);vector_stack.pop();
        for(int i=1199;i>=0;--i){CHECK(vector_stack.top().id==(i^0x5a5));vector_stack.pop();}
        CHECK(vector_stack.empty());
        std::stack<unsigned> a,b;
        a.push(1);a.push(4);b.push(1);b.push(4);CHECK(a==b);b.top()=5;CHECK(a<b);
    }
    return 0;
}

static int cases()
{
    CHECK(!bits<0>() && !bits<1>() && !bits<63>() && !bits<64>() && !bits<65>() && !bits<129>() && !bits<257>());
    std::bitset<129> small(0x91a5f03u);CHECK(small.to_ulong()==0x91a5f03u && small.to_ullong()==0x91a5f03u);
    std::bitset<8> substring(std::string("xx101100yy"),2,6);CHECK(substring.to_ulong()==44);
    std::bitset<4> truncated("101100");CHECK(truncated.to_ulong()==11);
    std::bitset<65> custom("baab",4,'a','b');CHECK(custom.to_ullong()==9);
    CHECK(!stacks());return 0;
}

static void* worker(void*){return reinterpret_cast<void*>(static_cast<uintptr_t>(cases()));}

int main()
{
    CHECK(!cases());CHECK(!live.load() && created.load()==destroyed.load());
    puts("[ OK ] actual bitset boundary/model and deque/vector stack lifetime cases passed");
    pthread_t threads[3];pthread_attr_t attr;
    CHECK(!pthread_attr_init(&attr) && !pthread_attr_setstacksize(&attr,128*1024));
    for(unsigned i=0;i<3;++i)CHECK(!pthread_create(&threads[i],&attr,worker,nullptr));
    for(unsigned i=0;i<3;++i){void *result=nullptr;CHECK(!pthread_join(threads[i],&result) && !result);}
    CHECK(!pthread_attr_destroy(&attr));
    CHECK(!live.load() && created.load()==destroyed.load());
#ifdef __CHOS__
    chos_heap_stats_t stats;CHECK(!chos_libc_heap_info(&stats) && !stats.allocations && !stats.mapped_bytes);
#endif
    printf("[LIFETIME] created=%d destroyed=%d live=%d\n",created.load(),destroyed.load(),live.load());
    puts("[ OK ] actual stdlib container workers and allocations cleaned up");
    return 0;
}
