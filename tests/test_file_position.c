#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "chos/file_position.h"
static uint64_t state=UINT64_C(0x504f534954494f4e);
static uint64_t random_value(void){state^=state<<13;state^=state>>7;state^=state<<17;return state;}
int main(void){
 uint64_t out=123;assert(!chos_file_position(0,42,0,CHOS_SEEK_END,&out)&&out==42);
 assert(!chos_file_position(0,0,INT64_MAX,CHOS_SEEK_SET,&out)&&out==INT64_MAX);
 assert(chos_file_position(INT64_MAX,0,1,CHOS_SEEK_CUR,&out)==-CHOS_ERRNO_EOVERFLOW&&out==INT64_MAX);
 assert(chos_file_position(INT64_MAX,0,INT64_MIN,CHOS_SEEK_CUR,&out)==-CHOS_ERRNO_EINVAL&&out==INT64_MAX);
 assert(chos_file_position(0,0,0,3,&out)==-CHOS_ERRNO_EINVAL);
 assert(chos_file_position(0,UINT64_MAX,0,CHOS_SEEK_END,&out)==-CHOS_ERRNO_EOVERFLOW);
 assert(chos_file_position(0,0,0,0,0)==-CHOS_ERRNO_EINVAL);
 for(unsigned i=0;i<1000000;i++){
  uint64_t current=random_value()&INT64_MAX,size=random_value()&INT64_MAX;int64_t delta=(int64_t)random_value();unsigned origin=random_value()%4;
  out=UINT64_MAX;int r=chos_file_position(current,size,delta,origin,&out);__int128 expected=(origin==0?0:origin==1?current:size)+(__int128)delta;
  if(origin==3)assert(r==-CHOS_ERRNO_EINVAL&&out==UINT64_MAX);
  else if(expected<0)assert(r==-CHOS_ERRNO_EINVAL&&out==UINT64_MAX);
  else if(expected>INT64_MAX)assert(r==-CHOS_ERRNO_EOVERFLOW&&out==UINT64_MAX);
  else assert(!r&&out==(uint64_t)expected);
 }
 puts("file positions: signed64 extremes,all origins,overflow,no mutation on errors and1,000,000 independent128-bit reference cases passed");return 0;
}
