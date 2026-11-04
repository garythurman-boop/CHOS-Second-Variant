struct WatchdogState {unsigned calls,terminateAt;JSContextGroupRef group=nullptr;bool reset=false,clear=false,failed=false;};
static bool terminate(JSContextRef context,void* opaque) {
 auto& state=*static_cast<WatchdogState*>(opaque);++state.calls;
 if(state.clear){
  JSContextGroupClearExecutionTimeLimit(state.group);
  auto name=JSStringCreateWithUTF8CString("stop");JSValueRef exception=nullptr;
  JSObjectSetProperty(context,JSContextGetGlobalObject(context),name,JSValueMakeBoolean(context,true),kJSPropertyAttributeNone,&exception);JSStringRelease(name);
  state.failed=!!exception;return false;
 }
 if(state.reset&&state.calls==1)JSContextGroupSetExecutionTimeLimit(state.group,.25,terminate,&state);
 return state.calls>=state.terminateAt;
}
static bool watchdog() {
 for(unsigned threshold: {1u,2u}) {
  auto group=JSContextGroupCreate();auto context=JSGlobalContextCreateInGroup(group,nullptr);if(!context){JSContextGroupRelease(group);return false;}
  WatchdogState state{0,threshold,group};JSContextGroupSetExecutionTimeLimit(group,.50,terminate,&state);
  std::puts("[INFO] actual native JSC long script begins");
  auto script=JSStringCreateWithUTF8CString("for(;;){}");JSValueRef exception=nullptr;
  auto result=JSEvaluateScript(context,script,nullptr,nullptr,1,&exception);JSStringRelease(script);
  bool ok=!result&&exception&&state.calls==threshold;
  JSContextGroupClearExecutionTimeLimit(group);JSGarbageCollect(context);JSGlobalContextRelease(context);JSContextGroupRelease(group);
  std::printf("[WATCHDOG] threshold=%u calls=%u terminated=%d\n",threshold,state.calls,ok);
  if(!ok)return false;
 }
 for(unsigned mode=0;mode<3;++mode){
  auto group=JSContextGroupCreate();auto context=JSGlobalContextCreateInGroup(group,nullptr);if(!context){JSContextGroupRelease(group);return false;}
  WatchdogState state{0,2,group,mode==1,mode==2};
  JSContextGroupSetExecutionTimeLimit(group,.50,mode?terminate:nullptr,mode?&state:nullptr);
  auto script=JSStringCreateWithUTF8CString(mode==2?"globalThis.stop=false;while(!globalThis.stop){};6*7":"for(;;){}");JSValueRef exception=nullptr;
  auto result=JSEvaluateScript(context,script,nullptr,nullptr,1,&exception);JSStringRelease(script);
  bool ok=mode==2?(result&&!exception&&state.calls==1&&!state.failed&&JSValueToNumber(context,result,nullptr)==42):(!result&&exception&&state.calls==(mode==1?2u:0u));
  if(mode==2)ok=ok&&evaluate(context,"if(21*2!==42)throw new Error('cleared watchdog context unusable');","chos-cleared-watchdog");
  JSContextGroupClearExecutionTimeLimit(group);JSGarbageCollect(context);JSGlobalContextRelease(context);JSContextGroupRelease(group);
  std::printf("[WATCHDOG] %s calls=%u passed=%d\n",mode==0?"default":mode==1?"reset":"clear",state.calls,ok);
  if(!ok)return false;
 }
 std::puts("[ OK ] actual native JSC CPU watchdog terminates long scripts and respects callback continuation");return true;
}
