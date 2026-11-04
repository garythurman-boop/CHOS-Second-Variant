#include "config.h"
#include <JavaScriptCore/JavaScript.h>
#include <JSContextRefPrivate.h>
#include <wtf/RunLoop.h>
#include <wtf/Threading.h>
#include <chos/user.h>
#include <cstdio>
#include <cstring>
#include <string>
#include "cases.h"
static unsigned completed,asyncErrors;
static bool awaitingAsync;
static void describe(JSContextRef context,JSValueRef value) {
    JSValueRef nested=nullptr;auto string=value?JSValueToStringCopy(context,value,&nested):nullptr;char buffer[512]{};
    if(string){JSStringGetUTF8CString(string,buffer,sizeof(buffer));JSStringRelease(string);}
    std::printf("[DETAIL] %s\n",buffer[0]?buffer:"<unavailable>");
}
static JSValueRef print(JSContextRef context,JSObjectRef,JSObjectRef,size_t argc,const JSValueRef argv[],JSValueRef* exception) {
    for(size_t i=0;i<argc;++i){
        auto string=JSValueToStringCopy(context,argv[i],exception);if(!string)return JSValueMakeUndefined(context);
        char buffer[512]{};JSStringGetUTF8CString(string,buffer,sizeof(buffer));JSStringRelease(string);
        if(!std::strcmp(buffer,"Test262:AsyncTestComplete"))++completed;
        else if(!std::strncmp(buffer,"Test262:AsyncTestFailure:",24))++asyncErrors;
        std::printf("[TEST262 PRINT] %s\n",buffer);
    }
    if(awaitingAsync&&(completed||asyncErrors))WTF::RunLoop::currentSingleton().stop();
    return JSValueMakeUndefined(context);
}
static bool installPrint(JSGlobalContextRef context) {
    auto name=JSStringCreateWithUTF8CString("print");auto function=JSObjectMakeFunctionWithCallback(context,name,print);JSValueRef exception=nullptr;
    JSObjectSetProperty(context,JSContextGetGlobalObject(context),name,function,kJSPropertyAttributeNone,&exception);JSStringRelease(name);return !exception;
}
static bool evaluate(JSGlobalContextRef context,const char* source,const char* name,bool syntaxOnly=false,const char* expected="") {
    auto script=JSStringCreateWithUTF8CString(source);auto url=JSStringCreateWithUTF8CString(name);JSValueRef exception=nullptr;
    bool ok=syntaxOnly?JSCheckScriptSyntax(context,script,url,1,&exception):!!JSEvaluateScript(context,script,nullptr,url,1,&exception);
    JSStringRelease(url);JSStringRelease(script);
    if(*expected){
        if(ok||!exception)return false;
        auto constructorName=JSStringCreateWithUTF8CString(expected);JSValueRef nested=nullptr;
        auto constructor=JSObjectGetProperty(context,JSContextGetGlobalObject(context),constructorName,&nested);JSStringRelease(constructorName);
        if(!nested&&constructor&&JSValueIsObject(context,constructor)&&JSValueIsInstanceOfConstructor(context,exception,JSValueToObject(context,constructor,&nested),&nested)&&!nested)return true;
    }else if(ok&&!exception)return true;
    describe(context,exception);return false;
}
static bool run(const Case& test,bool strict) {
    auto context=JSGlobalContextCreate(nullptr);if(!context)return false;
    completed=asyncErrors=0;bool ok=true;
    ok=installPrint(context);
    // A captured actual engine transfer operation detaches the supplied buffer.
    // If the intrinsic is unavailable, the original helper reports a real failure.
    ok=ok&&evaluate(context,"globalThis.$262 = (()=>{const transfer=ArrayBuffer.prototype.transfer;return {detachArrayBuffer(buffer){if(typeof transfer!=='function')throw new Error('Native detach hook unavailable');Reflect.apply(transfer,buffer,[0]);}};})();","chos-native-detach-hook");
    for(unsigned i=0;ok&&i<2;++i)ok=evaluate(context,helpers[i].text,helpers[i].name);
    if(ok&&test.asynchronous)ok=evaluate(context,helpers[2].text,helpers[2].name);
    for(int i:test.includes){if(i<0)break;if(ok)ok=evaluate(context,helpers[i].text,helpers[i].name);}
    if(ok){
        std::string text=strict?"\"use strict\";\n":"";text+=test.text;
        ok=evaluate(context,text.c_str(),test.name,test.negative[0]!=0,test.negative);
    }
    if(ok&&test.asynchronous){
        // C API entries drain actual Promise jobs. Bound any remaining entries;
        // absence,duplicate completion or an original async failure cannot pass.
        for(unsigned i=0;i<100&&!completed&&!asyncErrors;++i){ok=evaluate(context,"void 0","chos-promise-drain");if(!ok)break;chos_sleep_ticks(1);}
        ok=ok&&completed==1&&!asyncErrors;
    }
    JSGarbageCollect(context);JSGlobalContextRelease(context);
    return ok;
}
#include "wasm.inc"
#include "watchdog.inc"
#include "async.inc"
int main(int argc,char** argv){
    WTF::initialize();WTF::RunLoop::initializeMain();
    if(argc>1&&!std::strcmp(argv[1],"wasm"))return wasm()?0:3;
    if(argc>1&&!std::strcmp(argv[1],"watchdog"))return watchdog()?0:3;
    if(argc>1&&!std::strcmp(argv[1],"async-wasm"))return asyncWasm()?0:3;
    if(argc>1&&!std::strcmp(argv[1],"mixed")){
        if(!wasm()||!asyncWasm()||!watchdog())return 3;
        std::puts("[ OK ] actual native Wasm,async main loop and CPU watchdog coexist");return 0;
    }
    if(argc>1&&std::strcmp(argv[1],"test262"))return 2;
    unsigned passed=0,failed=0;
    std::puts("[INFO] actual native Test262 selected suite begins");
    for(const auto& test:cases)for(unsigned mode=0;mode<2;++mode)if(test.modes&(1u<<mode)){
        bool ok=run(test,mode==1);if(ok)++passed;else ++failed;
        std::printf("[TEST262 %s] %s %s\n",ok?"PASS":"FAIL",mode?"strict":"default",test.name);chos_yield();
    }
    std::printf("[TEST262 TOTAL] passed=%u failed=%u\n",passed,failed);
    if(failed)return 3;
    std::puts("[ OK ] actual pinned Test262 selected native suite executed");return 0;
}
