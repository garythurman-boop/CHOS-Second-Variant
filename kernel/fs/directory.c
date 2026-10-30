#include "chos/directory_snapshot.h"
#include "chos/storage.h"
#include "chos/romfs.h"
#include "chos/kheap.h"
#include "chos/syscall.h"
#define MAX_ENTRIES (CHOS_CHFS_MAX_FILES+CHOS_ROMFS_MAX_FILES)
typedef struct {
 chos_chfs_entry_t catalogue[CHOS_CHFS_MAX_FILES];
 chos_file_list_entry_t entries[MAX_ENTRIES];
} directory_work_t;
int chos_directory_collect_page(const char *path,const chos_directory_page_t *request,chos_directory_page_t *out){
 if(!path||!request||!out||request->version!=CHOS_DIRECTORY_VERSION||request->size_bytes!=sizeof(*request)||
    request->reserved[0]||request->reserved[1]||request->flags&~CHOS_DIRECTORY_FLAGS)return -CHOS_ERRNO_EINVAL;
 int initial=(request->flags&CHOS_DIRECTORY_START)!=0;
 if(initial&&(request->flags!=CHOS_DIRECTORY_START||request->mount||request->revision||request->index))return -CHOS_ERRNO_EINVAL;
 if(romfs_file_count()>CHOS_ROMFS_MAX_FILES)return -CHOS_ERRNO_E2BIG;
 directory_work_t *work=kheap_alloc(sizeof(*work));if(!work)return -CHOS_ERRNO_ENOMEM;
 chos_directory_builder_t view;uint64_t mount,revision;unsigned state;
 int result=chos_directory_builder_init(&view,path,work->entries,MAX_ENTRIES);
 if(result)goto done;
 result=storage_copy_catalogue(work->catalogue,CHOS_CHFS_MAX_FILES,&mount,&revision,&state);
 if(result)goto done;
 if(!initial&&(request->mount!=mount||request->revision!=revision||
    (request->flags&(CHOS_DIRECTORY_READ_ONLY|CHOS_DIRECTORY_CHFS_PRESENT))!=state)){
  result=-CHOS_ERRNO_ESTALE;goto done;
 }
 for(size_t i=0;i<CHOS_CHFS_MAX_FILES;i++)if(work->catalogue[i].path[0]){
  chos_chfs_entry_t *entry=work->catalogue+i;
  unsigned flags=CHOS_FILE_FLAG_PERSISTENT;
  if(state&CHOS_DIRECTORY_READ_ONLY)flags|=CHOS_FILE_FLAG_READ_ONLY;
  if(chfs_entry_is_directory(entry))flags|=CHOS_FILE_FLAG_DIRECTORY;
  result=chos_directory_builder_add(&view,entry->path,entry->size_bytes,flags);if(result)goto done;
 }
 for(size_t i=0;i<romfs_file_count();i++){
  chos_romfs_file_t entry;
  if(!romfs_get_at(i,&entry)||entry.size>UINT32_MAX){result=-CHOS_ERRNO_EIO;goto done;}
  result=chos_directory_builder_add(&view,entry.path,(uint32_t)entry.size,CHOS_FILE_FLAG_READ_ONLY);if(result)goto done;
 }
 result=chos_directory_builder_finish(&view);if(result)goto done;
 if(request->index>view.count){result=-CHOS_ERRNO_EINVAL;goto done;}
 chos_directory_page_t value={.version=CHOS_DIRECTORY_VERSION,.size_bytes=sizeof(value),.flags=state,
  .mount=mount,.revision=revision,.index=request->index,.total=view.count};
 while(value.count<CHOS_DIRECTORY_PAGE_ENTRIES&&value.index<view.count)value.entries[value.count++]=view.entries[value.index++];
 if(value.index==view.count)value.flags|=CHOS_DIRECTORY_END;
 *out=value;
 done:
 (void)kheap_free(work);return result;
}
