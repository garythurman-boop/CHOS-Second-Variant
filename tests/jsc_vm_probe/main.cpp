#include <JavaScriptCore/JavaScript.h>
#include <cstdio>
static bool evaluate(JSGlobalContextRef context,const char* text) {
    auto script=JSStringCreateWithUTF8CString(text);JSValueRef exception=nullptr;
    auto value=JSEvaluateScript(context,script,nullptr,nullptr,1,&exception);
    JSStringRelease(script);
    return !exception && value && JSValueToBoolean(context,value);
}
int main() {
    auto context=JSGlobalContextCreate(nullptr);
    if(!context) return 2;
    bool basic=evaluate(context,"(()=>{let n=0;for(let i=0;i<100;i++)n+=i;return n===4950&&JSON.parse(JSON.stringify({name:'CHOS'})).name==='CHOS';})()");
    bool intl=evaluate(context,"new Intl.DateTimeFormat('en-US',{timeZone:'UTC',year:'numeric'}).format(new Date(0))==='1970'");
    JSGarbageCollect(context);JSGlobalContextRelease(context);
    if(!basic||!intl) return 3;
    puts("[ OK ] actual JavaScriptCore VM,JavaScript and full ICU Intl executed");return 0;
}
