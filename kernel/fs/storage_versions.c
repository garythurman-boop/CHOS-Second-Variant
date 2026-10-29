#include "chos/storage_versions.h"
static int same(const chos_chfs_entry_t *a,const chos_chfs_entry_t *b){
 const uint8_t *x=(const uint8_t *)a,*y=(const uint8_t *)b;
 for(size_t i=0;i<sizeof(*a);i++)if(x[i]!=y[i])return 0;
 return 1;
}
static int room(const chos_storage_versions_t *v,uint64_t count){return v&&count<=UINT64_MAX-v->last_issued;}
void chos_storage_versions_invalidate(chos_storage_versions_t *v){
 if(!v)return;
 v->live=0;v->mount=0;
 for(size_t i=0;i<CHOS_CHFS_MAX_FILES;i++)v->objects[i]=v->revisions[i]=0;
}
int chos_storage_versions_mount(chos_storage_versions_t *v,const chos_chfs_entry_t *entries){
 if(!v||!entries)return 0;
 uint64_t count=1;
 for(size_t i=0;i<CHOS_CHFS_MAX_FILES;i++)if(entries[i].path[0])count+=2;
 if(!room(v,count))return 0;
 v->mount=++v->last_issued;
 for(size_t i=0;i<CHOS_CHFS_MAX_FILES;i++){
  v->objects[i]=v->revisions[i]=0;
  if(entries[i].path[0]){v->objects[i]=++v->last_issued;v->revisions[i]=++v->last_issued;}
 }
 v->live=1;return 1;
}
int chos_storage_versions_can_mutate(const chos_storage_versions_t *v){
 return v&&v->live&&v->mount&&room(v,CHOS_CHFS_MAX_FILES*2u);
}
int chos_storage_versions_update(chos_storage_versions_t *v,const chos_chfs_entry_t *before,
 const chos_chfs_entry_t *after,size_t force_index){
 if(!v||!v->live||!v->mount||!before||!after||(force_index!=SIZE_MAX&&force_index>=CHOS_CHFS_MAX_FILES))return 0;
 uint64_t count=0;
 for(size_t i=0;i<CHOS_CHFS_MAX_FILES;i++)if(i==force_index||!same(before+i,after+i)){
  ++count;if(after[i].path[0]&&!before[i].path[0])++count;
 }
 if(!room(v,count))return 0;
 for(size_t i=0;i<CHOS_CHFS_MAX_FILES;i++)if(i==force_index||!same(before+i,after+i)){
  v->revisions[i]=++v->last_issued;
  if(!after[i].path[0])v->objects[i]=0;
  else if(!before[i].path[0])v->objects[i]=++v->last_issued;
 }
 return 1;
}
int chos_storage_versions_get(const chos_storage_versions_t *v,size_t index,chos_storage_lease_t *out){
 if(!v||!out||!v->live||!v->mount||index>=CHOS_CHFS_MAX_FILES||!v->objects[index]||!v->revisions[index])return 0;
 *out=(chos_storage_lease_t){.mount=v->mount,.object=v->objects[index],.revision=v->revisions[index],.index=(uint32_t)index};return 1;
}
int chos_storage_versions_match(const chos_storage_versions_t *v,const chos_storage_lease_t *lease){
 return v&&lease&&v->live&&v->mount&&lease->mount==v->mount&&!lease->reserved&&
  lease->index<CHOS_CHFS_MAX_FILES&&lease->object&&lease->revision&&
  v->objects[lease->index]==lease->object&&v->revisions[lease->index]==lease->revision;
}
