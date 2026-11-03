#include <JavaScriptCore/JavaScript.h>
#include <cstdio>
static bool evaluate(JSGlobalContextRef context,const char* text) {
    auto script=JSStringCreateWithUTF8CString(text);JSValueRef exception=nullptr;
    auto value=JSEvaluateScript(context,script,nullptr,nullptr,1,&exception);
    JSStringRelease(script);
    if(exception || !value || !JSValueToBoolean(context,value)) {
        JSValueRef nested=nullptr;auto text=exception?exception:value;
        auto string=text?JSValueToStringCopy(context,text,&nested):nullptr;
        char buffer[512]{};
        if(string) {JSStringGetUTF8CString(string,buffer,sizeof(buffer));JSStringRelease(string);}
        std::printf("[DETAIL] actual JSC %s: %s\n",exception?"exception":"result",buffer[0]?buffer:"<unavailable>");
        return false;
    }
    return true;
}
int main() {
    puts("[INFO] actual JavaScriptCore VM creation begins");
    auto context=JSGlobalContextCreate(nullptr);
    if(!context) return 2;
    puts("[INFO] actual JavaScriptCore VM created;JavaScript begins");
    bool basic=evaluate(context,"(()=>{let n=0;for(let i=0;i<100;i++)n+=i;return n===4950&&JSON.parse(JSON.stringify({name:'CHOS'})).name==='CHOS';})()");
    puts(basic?"[INFO] actual JavaScript evaluation passed":"[FAIL] actual JavaScript evaluation");
    bool intl=evaluate(context,"new Intl.DateTimeFormat('en-US',{timeZone:'UTC',year:'numeric'}).format(new Date(0))==='1970'");
    puts(intl?"[INFO] actual ICU Intl evaluation passed":"[FAIL] actual ICU Intl evaluation");
    puts("[INFO] actual JavaScriptCore garbage collection/release begins");
    JSGarbageCollect(context);JSGlobalContextRelease(context);
    puts("[INFO] actual JavaScriptCore collection and context release completed");
    if(!basic||!intl) return 3;
    puts("[ OK ] actual JavaScriptCore VM,JavaScript and full ICU Intl executed");return 0;
}
