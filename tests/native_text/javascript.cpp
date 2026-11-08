#include <JavaScriptCore/JavaScript.h>
#include <JavaScriptCore/JSContextRefPrivate.h>
#include <cstdio>
#include <cstring>
static JSGlobalContextRef context;
extern "C" int chos_text_javascript_start() {
    context=JSGlobalContextCreate(nullptr);
    if(!context)return 0;
    std::puts("[ OK ] actual JavaScript text VM created");return 1;
}
extern "C" int chos_text_javascript_frame(unsigned epoch,char output[3][256],unsigned *height) {
    char script[2048];std::snprintf(script,sizeof(script),R"JS(
        (()=>{
            const lines=['office affinity AV fi fl',
                         '\u0627\u0644\u0633\u0644\u0627\u0645 \u0639\u0644\u064a\u0643\u0645',
                         '\u0928\u092e\u0938\u094d\u0924\u0947 \u0926\u0941\u0928\u093f\u092f\u093e'];
            return [...lines.map(s=>s.normalize('NFC')),[24,32,16][(%u-1)%%3]];
        })()
    )JS",epoch);
    auto group=JSContextGetGroup(context);JSContextGroupSetExecutionTimeLimit(group,5.0,nullptr,nullptr);
    auto text=JSStringCreateWithUTF8CString(script);JSValueRef exception=nullptr;
    auto value=JSEvaluateScript(context,text,nullptr,nullptr,1,&exception);JSStringRelease(text);
    JSContextGroupClearExecutionTimeLimit(group);
    if(!value||exception||!JSValueIsArray(context,value))return 0;
    JSValueProtect(context,value);auto array=JSValueToObject(context,value,&exception);bool ok=array&&!exception;
    for(unsigned i=0;i<3&&ok;++i){
        auto v=JSObjectGetPropertyAtIndex(context,array,i,&exception);
        if(exception||!v||!JSValueIsString(context,v)){ok=false;break;}
        auto string=JSValueToStringCopy(context,v,&exception);
        if(exception||!string){ok=false;break;}
        if(JSStringGetMaximumUTF8CStringSize(string)>256)ok=false;
        else if(!JSStringGetUTF8CString(string,output[i],256))ok=false;
        JSStringRelease(string);
    }
    if(ok){auto v=JSObjectGetPropertyAtIndex(context,array,3,&exception);double size=exception?0:JSValueToNumber(context,v,&exception);
        if(exception||!(size>=1&&size<=128)||size!=static_cast<unsigned>(size))ok=false;
        else *height=static_cast<unsigned>(size);}
    JSValueUnprotect(context,value);JSGarbageCollect(context);
    if(ok)std::printf("[TEXT ENGINE] actual JavaScript normalized frame %u at %u pixels\n",epoch,*height);
    return ok;
}
extern "C" void chos_text_javascript_stop() {
    JSGarbageCollect(context);JSGlobalContextRelease(context);context=nullptr;
    std::puts("[ OK ] actual JavaScript text VM released");
}
