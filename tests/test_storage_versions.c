#include "chos/storage_versions.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static chos_chfs_entry_t before[CHOS_CHFS_MAX_FILES],after[CHOS_CHFS_MAX_FILES];
static chos_storage_versions_t v,old;
static void entry(chos_chfs_entry_t *e,const char *path,unsigned crc){memset(e,0,sizeof(*e));strcpy(e->path,path);e->crc32=crc;}
static void copy(void){memcpy(before,after,sizeof before);}
int main(void){
 entry(after,"/A",1);entry(after+1,"/B",2);assert(chos_storage_versions_mount(&v,after));
 chos_storage_lease_t a,b,next;assert(chos_storage_versions_get(&v,0,&a)&&chos_storage_versions_get(&v,1,&b));
 assert(a.object!=b.object&&a.revision!=b.revision&&chos_storage_versions_match(&v,&a));
 copy();after[1].crc32=3;assert(chos_storage_versions_update(&v,before,after,SIZE_MAX));
 assert(chos_storage_versions_match(&v,&a)&&!chos_storage_versions_match(&v,&b));
 assert(chos_storage_versions_get(&v,1,&next)&&next.object==b.object);b=next;
 /* Even an identical-byte explicit write invalidates its old lease. */
 copy();assert(chos_storage_versions_update(&v,before,after,0));assert(!chos_storage_versions_match(&v,&a));
 assert(chos_storage_versions_get(&v,0,&next)&&next.object==a.object);a=next;
 /* Rename keeps object identity but invalidates its path-bound writable lease. */
 copy();strcpy(after[0].path,"/RENAMED");assert(chos_storage_versions_update(&v,before,after,SIZE_MAX));
 assert(!chos_storage_versions_match(&v,&a)&&chos_storage_versions_get(&v,0,&next)&&next.object==a.object);a=next;
 /* Delete/recreate, including an exact byte-for-byte metadata ABA, cannot
  * inherit the old identity or revision. Unrelated records stay valid. */
 copy();memset(after,0,sizeof after[0]);assert(chos_storage_versions_update(&v,before,after,SIZE_MAX));
 assert(!chos_storage_versions_match(&v,&a)&&!chos_storage_versions_get(&v,0,&next)&&chos_storage_versions_match(&v,&b));
 copy();entry(after,"/RENAMED",1);assert(chos_storage_versions_update(&v,before,after,SIZE_MAX));
 assert(chos_storage_versions_get(&v,0,&next)&&next.object!=a.object&&!chos_storage_versions_match(&v,&a));a=next;
 chos_storage_versions_invalidate(&v);assert(!chos_storage_versions_match(&v,&a));
 assert(chos_storage_versions_mount(&v,after)&&!chos_storage_versions_match(&v,&a));
 /* Independent simple reference: ordinary writes always retain identity and
  * replace only the selected revision, across10000 pseudo-random operations. */
 uint64_t identities[2],revisions[2],seed=47;
 for(size_t i=0;i<2;i++){assert(chos_storage_versions_get(&v,i,&next));identities[i]=next.object;revisions[i]=next.revision;}
 for(unsigned n=0;n<10000;n++){
  seed=seed*6364136223846793005ull+1;size_t index=(seed>>63)&1;copy();after[index].size_bytes=(uint32_t)(seed>>32);
  assert(chos_storage_versions_can_mutate(&v)&&chos_storage_versions_update(&v,before,after,index));
  for(size_t i=0;i<2;i++){assert(chos_storage_versions_get(&v,i,&next)&&next.object==identities[i]);
   if(i==index)assert(next.revision>revisions[i]);else assert(next.revision==revisions[i]);revisions[i]=next.revision;}
 }
 old=v;copy();assert(!chos_storage_versions_update(&v,before,after,CHOS_CHFS_MAX_FILES)&&!memcmp(&v,&old,sizeof v));
 next=(chos_storage_lease_t){0};assert(!chos_storage_versions_match(&v,&next));
 assert(!chos_storage_versions_get(&v,CHOS_CHFS_MAX_FILES,&next)&&!memcmp(&v,&old,sizeof v));
 assert(!chos_storage_versions_get(NULL,0,&next)&&!chos_storage_versions_get(&v,0,NULL));
 assert(chos_storage_versions_get(&v,0,&next));next.reserved=1;assert(!chos_storage_versions_match(&v,&next));
 next.reserved=0;next.index=UINT32_MAX;assert(!chos_storage_versions_match(&v,&next));
 /* Fail before mutation on exhausted64-bit identities; never wrap to a stale ID. */
 v.last_issued=UINT64_MAX;old=v;copy();after[0].crc32++;
 assert(!chos_storage_versions_can_mutate(&v)&&!chos_storage_versions_update(&v,before,after,SIZE_MAX)&&!memcmp(&v,&old,sizeof v));
 assert(!chos_storage_versions_mount(&v,after)&&!memcmp(&v,&old,sizeof v));
 puts("storage versions: independent records,identical writes,rename,delete/recreate ABA,remount,10000 operations and fail-closed exhaustion passed");
}
