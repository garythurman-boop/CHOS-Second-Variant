#include <errno.h>
#include <stdio.h>
#ifdef CHOS_HOST_TEST
long chos_filename_unlink(const char *);
long chos_filename_rmdir(const char *);
long chos_filename_rename(const char *,const char *);
#else
#include "chos/user.h"
#define chos_filename_unlink chos_unlink
#define chos_filename_rmdir chos_rmdir
#define chos_filename_rename chos_rename
#endif
static int result(long value){if(value<0){errno=(int)-value;return -1;}return 0;}
int remove(const char *path){
 if(!path){errno=EINVAL;return -1;}
 long rc=chos_filename_unlink(path);
 /* Only a confirmed directory enters the directory syscall. */
 if(rc==-EISDIR)rc=chos_filename_rmdir(path);
 return result(rc);
}
int rename(const char *source,const char *destination){
 if(!source||!destination){errno=EINVAL;return -1;}
 return result(chos_filename_rename(source,destination));
}
