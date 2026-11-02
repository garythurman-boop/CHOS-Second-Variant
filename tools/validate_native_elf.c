/* Host artifact inspection using the actual kernel executable policy.
 * This never runs the application or promises physical runtime admission. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include "chos/process.h"
int main(int argc,char **argv){
    if(argc!=3){fputs("usage: validate-native-elf FILE PROFILE_ID\n",stderr);return 2;}
    char *end;errno=0;unsigned long expected=strtoul(argv[2],&end,10);
    if(errno||!*argv[2]||*end||expected>UINT32_MAX)return 2;
    FILE *f=fopen(argv[1],"rb");if(!f)return 2;
    if(fseek(f,0,SEEK_END)){fclose(f);return 2;}
    long length=ftell(f);
    if(length<64||(unsigned long)length>CHOS_ELF_MAX_LOAD_FILE||fseek(f,0,SEEK_SET)){fclose(f);return 1;}
    void *bytes=malloc((size_t)length);if(!bytes){fclose(f);return 2;}
    if(fread(bytes,1,(size_t)length,f)!=(size_t)length){free(bytes);fclose(f);return 2;}
    fclose(f);chos_exec_image_info_t image;
    int valid=process_image_validate(bytes,(size_t)length,&image)&&image.version==CHOS_EXEC_VERSION_ELF&&
        image.elf.resource_profile==(uint32_t)expected;
    free(bytes);
    if(!valid){fputs("native ELF refused by actual kernel image/profile policy\n",stderr);return 1;}
    printf("{\"accepted\":true,\"resource_profile\":%u,\"file_bytes\":%ld,\"virtual_bytes\":%u,\"root_stack_pages\":%u,\"segments\":%u,\"executed\":false}\n",
        image.elf.resource_profile,length,image.virtual_size,(unsigned)image.stack_pages,image.elf.segment_count);
    return 0;
}
