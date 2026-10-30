#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "chos/directory_snapshot.h"
#include "chos/syscall.h"
static chos_file_list_entry_t entries[600];
static unsigned permutation[511];
static void complete_catalogue(void){
 for(unsigned seed=1;seed<=30;seed++){
  for(unsigned i=0;i<511;i++)permutation[i]=i;
  unsigned rng=seed;
  for(unsigned i=510;i;i--){rng=rng*1664525u+1013904223u;unsigned j=rng%(i+1),old=permutation[i];permutation[i]=permutation[j];permutation[j]=old;}
  chos_directory_builder_t v;assert(!chos_directory_builder_init(&v,"/many",entries,600));
  for(unsigned i=0;i<511;i++){
   char path[64];unsigned n=permutation[i];snprintf(path,sizeof path,"/MANY/F%03u.BIN",n);
   assert(!chos_directory_builder_add(&v,path,n,CHOS_FILE_FLAG_PERSISTENT));
   assert(!chos_directory_builder_add(&v,path,999,CHOS_FILE_FLAG_READ_ONLY));
  }
  assert(!chos_directory_builder_finish(&v)&&v.count==511);
  for(unsigned i=0;i<511;i++){
   char expected[64];snprintf(expected,sizeof expected,"/MANY/F%03u.BIN",i);
   assert(!strcmp(entries[i].path,expected)&&entries[i].size_bytes==i&&entries[i].flags==CHOS_FILE_FLAG_PERSISTENT);
  }
 }
}
static void namespace_rules(void){
 chos_directory_builder_t v;
 assert(chos_directory_builder_init(&v,"/../BAD",entries,600)==-CHOS_ERRNO_EINVAL);
 assert(!chos_directory_builder_init(&v,"/EMPTY",entries,600));
 assert(!chos_directory_builder_add(&v,"/EMPTY",0,CHOS_FILE_FLAG_PERSISTENT|CHOS_FILE_FLAG_DIRECTORY));
 assert(!chos_directory_builder_finish(&v)&&v.count==0);
 assert(!chos_directory_builder_init(&v,"/MISSING",entries,600));assert(chos_directory_builder_finish(&v)==-CHOS_ERRNO_ENOENT);
 assert(!chos_directory_builder_init(&v,"/BIN",entries,600));
 assert(!chos_directory_builder_add(&v,"/BIN/HELLO.CHX",9,CHOS_FILE_FLAG_READ_ONLY));
 assert(!chos_directory_builder_add(&v,"/BIN",3,CHOS_FILE_FLAG_PERSISTENT));
 assert(chos_directory_builder_finish(&v)==-CHOS_ERRNO_ENOTDIR);
 for(unsigned reverse=0;reverse<2;reverse++){
  assert(!chos_directory_builder_init(&v,"/",entries,600));
  for(unsigned i=0;i<2;i++){
   if(i==reverse)assert(!chos_directory_builder_add(&v,"/BIN/HELLO.CHX",9,CHOS_FILE_FLAG_READ_ONLY));
   else assert(!chos_directory_builder_add(&v,"/bin",3,CHOS_FILE_FLAG_PERSISTENT));
  }
  assert(!chos_directory_builder_add(&v,"/Z/DEEP/ONE",4,CHOS_FILE_FLAG_PERSISTENT));
  assert(!chos_directory_builder_add(&v,"/Z/DEEP/TWO",5,CHOS_FILE_FLAG_PERSISTENT));
  assert(!chos_directory_builder_finish(&v)&&v.count==2);
  assert(!strcmp(entries[0].path,"/Z")&&entries[0].flags==(CHOS_FILE_FLAG_DIRECTORY|CHOS_FILE_FLAG_PERSISTENT));
  assert(!strcmp(entries[1].path,"/bin")&&entries[1].flags==CHOS_FILE_FLAG_PERSISTENT&&entries[1].size_bytes==3);
 }
 for(unsigned reverse=0;reverse<2;reverse++){
  assert(!chos_directory_builder_init(&v,"/NEST",entries,600));
  for(unsigned i=0;i<2;i++){
   if(i==reverse)assert(!chos_directory_builder_add(&v,"/NEST",7,CHOS_FILE_FLAG_READ_ONLY));
   else assert(!chos_directory_builder_add(&v,"/NEST/CHILD",3,CHOS_FILE_FLAG_PERSISTENT));
  }
  assert(!chos_directory_builder_finish(&v)&&v.count==1);
 }
 assert(!chos_directory_builder_init(&v,"/A",entries,1));
 assert(!chos_directory_builder_add(&v,"/AB/OTHER",0,1));
 assert(!chos_directory_builder_add(&v,"/A/ONE",0,1));
 assert(chos_directory_builder_add(&v,"/A/TWO",0,1)==-CHOS_ERRNO_E2BIG);
 assert(chos_directory_builder_finish(&v)==-CHOS_ERRNO_E2BIG);
}
int main(void){complete_catalogue();namespace_rules();puts("directory snapshots: complete seeded catalogues and namespace precedence passed");}
