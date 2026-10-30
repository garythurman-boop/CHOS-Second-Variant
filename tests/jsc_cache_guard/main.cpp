#include "config.h"
#include <JavaScriptCore/ARM64Assembler.h>
#include <wtf/InlineASM.h>
#include <chos/user.h>
#include <cstdio>
#include <cstring>
#if ENABLE(JIT)
#error "This fixture requires the native interpreter profile"
#endif
#if !ENABLE(ASSEMBLER) || !CPU(ARM64) || !OS(CHOS)
#error "This fixture must exercise the actual ARM64 CHOS assembler header"
#endif
extern "C" {
uintptr_t chos_jsc_guard_word=UINT64_C(0x43484f53454c4631);
uintptr_t *chos_jsc_guard_address();
}
// The same ELF GOT/local-label/hidden-symbol syntax selected by offlineasm.
// This checks the actual native linker and guest addresses, not a host pointer.
asm(".text\n.balign 4\n.globl " SYMBOL_STRING(chos_jsc_guard_address) "\n"
 HIDE_SYMBOL(chos_jsc_guard_address) "\n" SYMBOL_STRING(chos_jsc_guard_address) ":\n"
 "adrp x0, :got:" LABEL_REFERENCE(chos_jsc_guard_word) "\n"
 "ldr x0, [x0, :got_lo12:" LABEL_REFERENCE(chos_jsc_guard_word) "]\n"
 "b " LOCAL_LABEL_STRING(chos_jsc_guard_return) "\n"
 "brk #0\n" LOCAL_LABEL_STRING(chos_jsc_guard_return) ":\nret\n");
int main(int argc,char **argv){
 if(chos_jsc_guard_address()!=&chos_jsc_guard_word||*chos_jsc_guard_address()!=UINT64_C(0x43484f53454c4631)){
  puts("[FAIL] native ELF GOT address or local label");return 4;
 }
 ++*chos_jsc_guard_address();
 if(chos_jsc_guard_word!=UINT64_C(0x43484f53454c4632))return 5;

 if(argc>1&&!strcmp(argv[1],"flush")){
  puts("[ OK ] entering unsupported ARM64 cache publication path");
  JSC::ARM64Assembler::cacheFlush(nullptr,0);
  puts("[FAIL] unsupported ARM64 cache publication returned");return 2;
 }
 long pid=chos_spawn_limited("/CACHEGUARD.ELF","flush",CHOS_PROCESS_CAP_ALL);
 if(pid<=0||chos_wait((uint32_t)pid)!=134||chos_getstatus(pid)!=CHOS_STATUS_EXITED){
  puts("[FAIL] ARM64 cache publication did not abort only its child");return 1;
 }
 puts("[ OK ] actual ARM64 no-JIT cache publication guard isolated its child");return 0;
}
