#include <complex.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include "complex_vectors.h"
extern float _Complex chos_test_mulsc3(float,float,float,float);
extern float _Complex chos_test_divsc3(float,float,float,float);
extern double _Complex chos_test_muldc3(double,double,double,double);
extern double _Complex chos_test_divdc3(double,double,double,double);
static double from64(uint64_t u){union{uint64_t u;double f;}v={u};return v.f;}
static float from32(uint32_t u){union{uint32_t u;float f;}v={u};return v.f;}
int main(void){
 double largest[2]={0};unsigned cases=0;
 for(unsigned i=0;i<sizeof(complex_vectors)/sizeof(*complex_vectors);i++){
  const complex_vector_t *v=&complex_vectors[i];if(v->op>1)continue;
  double re,im,er,ei;
  if(v->width==64){double _Complex z=v->op?chos_test_divdc3(from64(v->ar),from64(v->ai),from64(v->br),from64(v->bi)):chos_test_muldc3(from64(v->ar),from64(v->ai),from64(v->br),from64(v->bi));re=creal(z);im=cimag(z);er=from64(v->re);ei=from64(v->im);}
  else {float _Complex z=v->op?chos_test_divsc3(from32(v->ar),from32(v->ai),from32(v->br),from32(v->bi)):chos_test_mulsc3(from32(v->ar),from32(v->ai),from32(v->br),from32(v->bi));re=crealf(z);im=cimagf(z);er=from32(v->re);ei=from32(v->im);}
  double scale=fmax(fabs(er),fabs(ei)),error=fmax(fabs(re-er),fabs(im-ei));double eps=error/((v->width==32?0x1p-23:0x1p-52)*scale);
  assert(isfinite(re)&&isfinite(im)&&eps<=v->error_eps);if(eps>largest[v->width==64])largest[v->width==64]=eps;++cases;
 }
 double _Complex q=chos_test_divdc3(1,2,0,0);assert(isinf(creal(q))&&isinf(cimag(q)));
 q=chos_test_muldc3(INFINITY,0,1,1);assert(isinf(creal(q))&&isinf(cimag(q)));
 float _Complex n=chos_test_divsc3(0,0,0,0);assert(isnan(crealf(n))&&isnan(cimagf(n)));
 printf("original complex helpers: %u MP-backed mul/div cases and special values passed; maximum epsilon error32=%g,64=%g\n",cases,largest[0],largest[1]);return 0;
}
