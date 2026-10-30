#include "chos/directory_snapshot.h"
#include "chos/romfs.h"
#include "chos/syscall.h"
#define EXPLICIT (UINT32_C(1)<<31)
static int folded(int c){return c>='A'&&c<='Z'?c+'a'-'A':c;}
static int compare(const char *a,const char *b){
 size_t i=0;while(a[i]&&b[i]){int d=folded((unsigned char)a[i])-folded((unsigned char)b[i]);if(d)return d;i++;}
 return (unsigned char)a[i]-(unsigned char)b[i];
}
int chos_directory_builder_init(chos_directory_builder_t *v,const char *path,chos_file_list_entry_t *entries,size_t capacity){
 if(!v||!path||!entries||!capacity)return -CHOS_ERRNO_EINVAL;
 if(!(path[0]=='/'&&!path[1])&&!romfs_path_is_valid(path))return -CHOS_ERRNO_EINVAL;
 *v=(chos_directory_builder_t){.entries=entries,.capacity=capacity,.exists=path[0]=='/'&&!path[1]?3:0};
 size_t i=0;do{v->path[i]=path[i];}while(path[i++]);return 0;
}
int chos_directory_builder_add(chos_directory_builder_t *v,const char *path,uint32_t bytes,uint32_t flags){
 if(!v||!path||!romfs_path_is_valid(path)||flags&~31u)return -CHOS_ERRNO_EINVAL;
 if(v->error)return v->error;
 if(!compare(v->path,path)){
  int rank=flags&CHOS_FILE_FLAG_PERSISTENT?2:1;
  if(flags&CHOS_FILE_FLAG_DIRECTORY){if(rank>v->exists)v->exists=rank;}
  else if(rank>v->not_directory)v->not_directory=rank;
  return 0;
 }
 size_t prefix=0;while(v->path[prefix])prefix++;
 int root=prefix==1;
 if(!root){
  for(size_t i=0;i<prefix;i++)if(folded((unsigned char)path[i])!=folded((unsigned char)v->path[i]))return 0;
  if(path[prefix]!='/')return 0;
 }
 size_t start=root?1:prefix+1,end=start;while(path[end]&&path[end]!='/')end++;
 if(end==start)return 0;
 int source_rank=flags&CHOS_FILE_FLAG_PERSISTENT?2:1;
 if(source_rank>v->exists)v->exists=source_rank;
 chos_file_list_entry_t candidate={0};
 for(size_t i=0;i<end;i++)candidate.path[i]=path[i];
 if(!path[end]){candidate.size_bytes=bytes;candidate.flags=flags|EXPLICIT;}
 else candidate.flags=(flags&(CHOS_FILE_FLAG_PERSISTENT|CHOS_FILE_FLAG_READ_ONLY))|CHOS_FILE_FLAG_DIRECTORY;
 /* Both paths have already passed canonical validation. Avoid repeating that
  * scan for every comparison in a large directory. */
 for(size_t i=0;i<v->count;i++)if(!compare(v->entries[i].path,candidate.path)){
  uint32_t old=v->entries[i].flags;
  int old_rank=((old&CHOS_FILE_FLAG_PERSISTENT)?2:0)+((old&EXPLICIT)?1:0);
  int rank=((candidate.flags&CHOS_FILE_FLAG_PERSISTENT)?2:0)+((candidate.flags&EXPLICIT)?1:0);
  if(rank>old_rank)v->entries[i]=candidate;
  return 0;
 }
 if(v->count==v->capacity){v->error=-CHOS_ERRNO_E2BIG;return v->error;}
 v->entries[v->count++]=candidate;return 0;
}
int chos_directory_builder_finish(chos_directory_builder_t *v){
 if(!v)return -CHOS_ERRNO_EINVAL;
 if(v->error)return v->error;
 if(v->not_directory&&v->not_directory>=v->exists)return -CHOS_ERRNO_ENOTDIR;
 if(!v->exists)return -CHOS_ERRNO_ENOENT;
 for(size_t i=0;i<v->count;i++)v->entries[i].flags&=~EXPLICIT;
 for(size_t i=1;i<v->count;i++){
  chos_file_list_entry_t value=v->entries[i];size_t j=i;
  while(j){
   uint32_t a=value.flags&CHOS_FILE_FLAG_DIRECTORY,b=v->entries[j-1].flags&CHOS_FILE_FLAG_DIRECTORY;
   if(!(a!=b?a>b:compare(value.path,v->entries[j-1].path)<0))break;
   v->entries[j]=v->entries[j-1];j--;
  }
  v->entries[j]=value;
 }
 return 0;
}
