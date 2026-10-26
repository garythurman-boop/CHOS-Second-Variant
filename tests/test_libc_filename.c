#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
int *__chos_errno_location(void){static int e;return &e;}
static long unlink_rc,rmdir_rc,rename_rc;
static unsigned unlinks,rmdirs,renames;
long chos_filename_unlink(const char *p){assert(!strcmp(p,"source"));++unlinks;return unlink_rc;}
long chos_filename_rmdir(const char *p){assert(!strcmp(p,"source"));++rmdirs;return rmdir_rc;}
long chos_filename_rename(const char *a,const char *b){assert(!strcmp(a,"source")&&!strcmp(b,"target"));++renames;return rename_rc;}
int main(void){
 errno=77;assert(!remove("source")&&errno==77&&unlinks==1&&!rmdirs);
 int errors[]={ENOENT,EPERM,EIO,EFAULT};
 for(unsigned i=0;i<sizeof(errors)/sizeof(*errors);i++){unlink_rc=-errors[i];assert(remove("source")==-1&&errno==errors[i]&&!rmdirs);}
 unlink_rc=-EISDIR;assert(!remove("source")&&rmdirs==1);
 rmdir_rc=-ENOTEMPTY;assert(remove("source")==-1&&errno==ENOTEMPTY&&rmdirs==2);
 unsigned n=unlinks;assert(remove(0)==-1&&errno==EINVAL&&unlinks==n);
 assert(!rename("source","target")&&renames==1);
 rename_rc=-EEXIST;assert(rename("source","target")==-1&&errno==EEXIST&&renames==2);
 assert(rename(0,"target")==-1&&errno==EINVAL&&renames==2);
 assert(rename("source",0)==-1&&errno==EINVAL&&renames==2);
 return 0;
}
