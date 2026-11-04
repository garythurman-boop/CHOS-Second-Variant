using namespace WTF::StringLiterals;
class AsyncDeadline final:public WTF::RunLoop::TimerBase {
public:
    bool expired=false;
    AsyncDeadline():TimerBase(WTF::Ref { WTF::RunLoop::currentSingleton() },"CHOS native async deadline"_s) { }
    void fired() final {expired=true;WTF::RunLoop::currentSingleton().stop();}
};
static bool asyncWasm() {
    auto context=JSGlobalContextCreate(nullptr);if(!context)return false;
    completed=asyncErrors=0;bool ok=installPrint(context);
    for(unsigned i=0;ok&&i<3;++i)ok=evaluate(context,helpers[i].text,helpers[i].name);
    if(ok){
        AsyncDeadline deadline;deadline.startOneShot(WTF::Seconds(5));awaitingAsync=true;
        // Dispatch on the real native main RunLoop;Wasm compilation,Promise jobs
        // and completion all use the original engine and its actual workers.
        WTF::RunLoop::currentSingleton().dispatch([context] {
            bool evaluated=evaluate(context,R"CHOSASYNC(
              const bytes=new Uint8Array([0,97,115,109,1,0,0,0,1,7,1,96,2,127,127,1,127,3,2,1,0,7,7,1,3,97,100,100,0,0,10,9,1,7,0,32,0,32,1,106,11]);
              WebAssembly.instantiate(bytes).then(({module,instance})=>{
                assert(module instanceof WebAssembly.Module);
                assert.sameValue(instance.exports.add(19,23),42);
                return WebAssembly.compile(bytes);
              }).then(module=>WebAssembly.instantiate(module)).then(instance=>{
                assert.sameValue(instance.exports.add(20,22),42);$DONE();
              },$DONE);
            )CHOSASYNC","chos-native-async-wasm");
            if(!evaluated){++asyncErrors;WTF::RunLoop::currentSingleton().stop();}
        });
        WTF::RunLoop::run();awaitingAsync=false;deadline.stop();
        ok=!deadline.expired&&completed==1&&!asyncErrors;
    }
    JSGarbageCollect(context);JSGlobalContextRelease(context);
    std::puts(ok?"[ OK ] actual native RunLoop,async Wasm compile/instantiate and Promise completion executed":"[FAIL] actual native async Wasm/main loop");return ok;
}
