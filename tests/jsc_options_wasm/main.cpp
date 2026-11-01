#include "config.h"
#include <JavaScriptCore/Options.h>
#include <JavaScriptCore/WasmFaultSignalHandler.h>
#include <wtf/Threading.h>
#include <chos/user.h>
#include <chos/libc.h>
#include <cstdio>
#include <cstring>
#include <thread>
#include <atomic>
#if !OS(CHOS) || !CPU(ARM64) || !ENABLE(WEBASSEMBLY) || ENABLE(JIT) || ENABLE(DFG_JIT) || ENABLE(FTL_JIT) || ENABLE(SAMPLING_PROFILER)
#error "This component requires the actual native checked/interpreter configuration"
#endif
static bool fail(unsigned line){printf("[FAIL] actual JSC options/checked Wasm line %u\n",line);return false;}
#define CHECK(v) do{if(!(v))return fail(__LINE__);}while(0)
static std::atomic<unsigned> initialized,errors;
static bool policy(){
 using JSC::Options;
 CHECK(Options::useWasm()&&Options::useWasmIPInt()&&Options::useLLInt());
 CHECK(!Options::useJIT()&&!Options::useDFGJIT()&&!Options::useFTLJIT()&&Options::usePollingTraps());
 CHECK(!Options::useWasmFaultSignalHandler()&&!Options::useWasmFastMemory());
 CHECK(JSC::Wasm::maxAcceptableOffsetForNullReference()==0);
 JSC::Wasm::prepareSignalingMemory();JSC::Wasm::activateSignalingMemory();return true;
}
static bool initialize(){
 WTF::initialize();
 JSC::Options::initialize([]{++initialized;JSC::Options::useWasmFaultSignalHandler()=true;JSC::Options::useWasmFastMemory()=true;});
 CHECK(initialized==1);return policy();
}
static bool worker(){
 JSC::Options::initialize([]{++initialized;});
 for(unsigned i=0;i<32;i++){CHECK(policy()&&initialized==1);CHECK(!chos_yield());}return true;
}
static bool run(){
 using JSC::Options;CHECK(initialize());
 CHECK(Options::setOption("gcMaxHeapSize=131072")&&Options::gcMaxHeapSize()==131072);
 CHECK(Options::setOption("smallHeapGrowthFactor=1.75")&&Options::smallHeapGrowthFactor()==1.75);
 auto old=Options::gcMaxHeapSize();CHECK(!Options::setOption("gcMaxHeapSize=unparseable")&&Options::gcMaxHeapSize()==old);
 CHECK(!Options::setOption("notAnActualCHOSJSCOption=true"));
 for(unsigned i=0;i<4;i++){
  CHECK(Options::setOption("useWasmFastMemory=TrUe"));CHECK(Options::setOption("useWasmFaultSignalHandler=YES"));
  CHECK(Options::setOption("useJIT=true")&&policy());
 }
 Options::assertOptionsAreCoherent();
 chos_heap_stats_t baseline;CHECK(!chos_libc_heap_info(&baseline));
 for(unsigned wave=0;wave<3;wave++){
  {std::thread a([]{if(!worker())++errors;}),b([]{if(!worker())++errors;}),c([]{if(!worker())++errors;});a.join();b.join();c.join();}
  CHECK(!errors&&initialized==1);chos_heap_stats_t heap;CHECK(!chos_libc_heap_info(&heap)&&heap.allocations==baseline.allocations&&heap.mapped_bytes==baseline.mapped_bytes);
 }
 for(const char *mode:{"force-prepare","force-activate"}){
  long pid=chos_spawn_args("/JSCOPTS.ELF",mode);CHECK(pid>0);
  CHECK(chos_wait((uint32_t)pid)==134&&chos_getstatus(pid)==CHOS_STATUS_EXITED&&policy());
 }
 puts("[ OK ] actual JSC option parsing,checked Wasm policy and native initialization lifecycles passed");return true;
}
int main(int argc,char **argv){
 if(argc>1&&(!strcmp(argv[1],"force-prepare")||!strcmp(argv[1],"force-activate"))){
  if(!initialize())return 1;
  puts("[ OK ] entering forced unsupported Wasm signal mode");
  JSC::Options::useWasmFaultSignalHandler()=true;
  if(!strcmp(argv[1],"force-prepare"))JSC::Wasm::prepareSignalingMemory();else JSC::Wasm::activateSignalingMemory();
  puts("[FAIL] unsupported Wasm signal mode succeeded");return 2;
 }
 return run()?0:1;
}
