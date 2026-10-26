#include "chos/user.h"
extern int fpu_probe(uint64_t pattern,uint64_t control);
static int fail(unsigned line){(void)line;const char *s="[FAIL] native FP context\n";chos_write(s,chos_strlen(s));return 1;}
#define CHECK(x) do {if(!(x))return fail(__LINE__);}while(0)
int main(int argc,char **argv){
 if(argc>1&&chos_strcmp(argv[1],"worker")==0){
  uint64_t pid=(uint64_t)chos_getpid();
  for(unsigned n=0;n<3;n++){
   CHECK(!fpu_probe(0x43484f5300000000ull+(pid<<12)+n*64u,((pid+n)%3u+1u)<<22));
   volatile double a=1.25,b=3.5,c=0.125;
   double result=a*b+c;
   CHECK(result==4.5);
   CHECK(!chos_yield());
   CHECK(result/a==3.6);
  }
  const char *s="[ OK ] native FP worker registers, controls and arithmetic passed\n";chos_write(s,chos_strlen(s));return 0;
 }
 for(unsigned n=0;n<2;n++){
  long first=chos_spawn_args("/BIN/FPTEST.ELF","worker");CHECK(first>0);
  long second=chos_spawn_args("/BIN/FPTEST.ELF","worker");CHECK(second>0&&second!=first);
  CHECK(chos_wait((uint32_t)first)==0);CHECK(chos_wait((uint32_t)second)==0);
 }
 const char *s="[ OK ] native FP/SIMD state isolation and fresh-process zeroing passed\n";chos_write(s,chos_strlen(s));return 0;
}
