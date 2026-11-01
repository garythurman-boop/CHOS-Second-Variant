#include "config.h"
#include <wtf/Threading.h>
#include <wtf/ThreadGroup.h>
#include <wtf/Locker.h>
#include <wtf/RefPtr.h>
#include <wtf/StackAllocation.h>
#include <chos/user.h>
#include <chos/libc.h>
#include <pthread.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <cerrno>
using namespace WTF;
static std::atomic<unsigned> errors;
static pthread_key_t key;
struct State {
    std::atomic<unsigned> ready {0}, release {0}, body {0}, phase {0};
    std::atomic<uint32_t> tid {0};
    Thread* thread {nullptr};
    size_t bytes {0};
    bool selfDetach {false}, selfJoin {false};
};
static bool fail(unsigned line) { printf("[FAIL] actual WTF threads line %u\n",line); return false; }
#define CHECK(v) do { if (!(v)) return fail(__LINE__); } while (0)
#define WORK_CHECK(v) do { if (!(v)) { ++errors;printf("[FAIL] WTF worker line %u\n",__LINE__); } } while (0)
static void pause() { if (chos_sleep_ticks(1)) { ++errors; chos_exit(9); } }
static bool gone(uint32_t tid) { chos_thread_info_t info{};return chos_thread_status(tid,&info)==-ESRCH; }
struct Local {
    State* state {nullptr};
    ~Local() { if (state) { unsigned zero=0;WORK_CHECK(state->phase.compare_exchange_strong(zero,1)); } }
};
static thread_local Local local;
static void destroyKey(void* value) {
    auto* s=static_cast<State*>(value);auto old=s->phase.fetch_add(1);
    WORK_CHECK(old==1 || old==2);
    if (old==1) WORK_CHECK(!pthread_setspecific(key,s));
}
static void body(State& s) {
    auto& current=Thread::currentSingleton();s.thread=&current;
    chos_thread_context_t c{};WORK_CHECK(!chos_thread_context(&c));
    WORK_CHECK(c.tid==(uint32_t)chos_thread_id() && c.tid);
    WORK_CHECK(current.stack().size()==s.bytes && current.stack().origin()==reinterpret_cast<void*>(c.stack_base+c.stack_bytes));
    WORK_CHECK(current.stack().contains(&c) && c.stack_bytes==s.bytes);
    WORK_CHECK(&Thread::currentSingleton()==&current && !current.isRealtime());
    local.state=&s;WORK_CHECK(!pthread_setspecific(key,&s));
    s.tid.store(c.tid);s.ready.store(1);
    while (!s.release.load()) pause();
    if (s.selfJoin) WORK_CHECK(current.waitForCompletion()==EDEADLK);
    if (s.selfDetach) current.detach();
    s.body.store(1);
}
static unsigned registry() { return Thread::allThreads().values().size(); }
static bool wave(unsigned kind) {
    printf("WTF worker wave kind=%u\n",kind);
    State states[3];RefPtr<Thread> workers[3];RefPtr<ThreadGroup> group=ThreadGroup::create();
    auto before=registry();
    for (unsigned i=0;i<3;i++) {
        auto& s=states[i];s.bytes=i==0?32768:i==1?65536:131072;s.selfDetach=kind==2;s.selfJoin=kind==3;
        auto spec=i?StackAllocationSpecification::RequestSize(s.bytes):StackAllocationSpecification{};
        // Capture actual Function state, not a replacement C entry point.
        workers[i]=Thread::create("CHOS.WTF.Lifecycle"_s,[&s] { body(s); },ThreadType::Unknown,Thread::defaultQOS,Thread::SchedulingPolicy::Other,spec);
        while (!s.ready.load()) pause();
        CHECK(workers[i].get()==s.thread && workers[i]->stack().size()==s.bytes);
        CHECK(group->add(*workers[i])==ThreadGroupAddResult::NewlyAdded);
        CHECK(group->add(*workers[i])==ThreadGroupAddResult::AlreadyAdded);
        CHECK(workers[i]->numberOfThreadGroups()==1);
    }
    CHECK(registry()==before+3);
    { Locker locker{group->getLock()};CHECK(group->threads(locker).size()==3); }
    CHECK(states[0].tid!=states[1].tid && states[1].tid!=states[2].tid && states[0].tid!=states[2].tid);
    if (kind==1) for (auto& worker:workers) worker->detach();
    for (auto& s:states) s.release.store(1);
    for (unsigned i=0;i<3;i++) {
        auto& s=states[i];auto& worker=workers[i];
        // Let the deliberately attempted self join finish before the parent
        // claims the join. A competing existing claim correctly returns EINVAL.
        if (kind==3) while (!s.body.load()) pause();
        if (kind==0 || kind==3) CHECK(!worker->waitForCompletion());
        else if (kind==4 || kind==5) {
            // Observe real kernel DONE before joining/detaching an exited worker.
            chos_thread_info_t info{};
            do { CHECK(!chos_thread_status(s.tid,&info));if(info.state!=CHOS_THREAD_EXITED) pause(); } while(info.state!=CHOS_THREAD_EXITED);
            if (kind==4) CHECK(!worker->waitForCompletion());else worker->detach();
        }
        while (!gone(s.tid)) pause();
        CHECK(s.body && s.phase==3);
        CHECK(worker->waitForCompletion()==EINVAL); // Guard before freed pthread TCB.
        // Original didExit removes group membership but retains its weak back-reference
        // until the group dies. Qualify that actual upstream lifetime too.
        CHECK(worker->numberOfThreadGroups()==1 && group->add(*worker)==ThreadGroupAddResult::NotAdded);
    }
    CHECK(registry()==before);
    { Locker locker{group->getLock()};CHECK(group->threads(locker).isEmpty()); }
    group=nullptr;
    for (auto& worker:workers) { CHECK(!worker->numberOfThreadGroups());worker=nullptr; }
    CHECK(!errors);return true;
}
static bool snapshot(chos_heap_stats_t& h,chos_resource_info_t& r) {
    CHECK(!chos_libc_heap_info(&h) && !chos_resource_info(&r));CHECK(!h.quarantined_regions);return true;
}
static bool rejected(const char* mode) {
    CHECK(Thread::currentSingleton().stack().size());
    CHECK(!pthread_key_create(&key,destroyKey));
    if (!strcmp(mode,"double-detach")) {
        State s;s.bytes=32768;
        auto t=Thread::create("CHOS.WTF.Refusal"_s,[&s] { body(s); });
        while (!s.ready) pause();t->detach();t->detach();
    } else if (!strcmp(mode,"quota")) {
        State s[4];RefPtr<Thread> t[4];
        for(unsigned i=0;i<4;i++){s[i].bytes=32768;t[i]=Thread::create("CHOS.WTF.Quota"_s,[&,i] { body(s[i]); });}
    } else {
        StackAllocationSpecification spec;
        auto policy=Thread::SchedulingPolicy::Other;
        static std::byte custom[65536];
        if(!strcmp(mode,"bad-size")) spec=StackAllocationSpecification::RequestSize(1024);
        else if(!strcmp(mode,"custom-stack")) spec=StackAllocationSpecification::CustomStack(custom);
        else if(!strcmp(mode,"policy")) policy=Thread::SchedulingPolicy::FIFO;
        else return false;
        Thread::create("CHOS.WTF.Unsupported"_s,[] { },ThreadType::Unknown,Thread::defaultQOS,policy,spec);
    }
    return fail(__LINE__); // Actual upstream RELEASE_ASSERT must terminate child.
}
static bool run() {
    auto& main=Thread::currentSingleton();CHECK(main.stack().size()==32768);
    CHECK(!pthread_key_create(&key,destroyKey));auto baselineThreads=registry();CHECK(baselineThreads==1);
    // Warm all actual original WTF tables/parking queues before the steady-state
    // assertion; preserve live root/global allocations rather than demand zero.
    for(unsigned kind=0;kind<6;kind++) CHECK(wave(kind));
    chos_heap_stats_t heap{};chos_resource_info_t resources{};CHECK(snapshot(heap,resources));
    for(unsigned round=0;round<3;round++) {
        for(unsigned kind=0;kind<6;kind++) CHECK(wave(kind));
        chos_heap_stats_t h{};chos_resource_info_t r{};CHECK(snapshot(h,r));
        CHECK(h.allocations==heap.allocations && h.requested_bytes==heap.requested_bytes && h.mapped_bytes==heap.mapped_bytes);
        CHECK(r.mapped_bytes==resources.mapped_bytes && r.resident_bytes==resources.resident_bytes);
    }
    CHECK(registry()==baselineThreads && !pthread_key_delete(key));
    for(auto mode:{"bad-size","custom-stack","policy","quota","double-detach"}) {
        long pid=chos_spawn_limited("/WTFTHR.ELF",mode,CHOS_PROCESS_CAP_ALL);CHECK(pid>0);
        CHECK(chos_wait(pid)==134 && chos_getstatus(pid)==CHOS_STATUS_EXITED);
    }
    printf("actual WTF warm heap: allocations=%zu bytes=%zu mapped=%zu\n",heap.allocations,heap.requested_bytes,heap.mapped_bytes);
    puts("[ OK ] actual WTF create,join,detach,stack,TLS and ThreadGroup lifecycle passed");return true;
}
int main(int argc,char** argv) {return (argc>1?rejected(argv[1]):run())?0:1;}
