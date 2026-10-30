#ifndef CHOS_DIRECTORY_SNAPSHOT_H
#define CHOS_DIRECTORY_SNAPSHOT_H
#include <stddef.h>
#include "chos/directory_api.h"
typedef struct {
 char path[CHOS_FILE_PATH_CAPACITY];
 chos_file_list_entry_t *entries;
 size_t count,capacity;
 int exists,not_directory,error;
} chos_directory_builder_t;
int chos_directory_builder_init(chos_directory_builder_t *,const char *,chos_file_list_entry_t *,size_t);
int chos_directory_builder_add(chos_directory_builder_t *,const char *,uint32_t,uint32_t);
int chos_directory_builder_finish(chos_directory_builder_t *);
int chos_directory_collect_page(const char *,const chos_directory_page_t *,chos_directory_page_t *);
#endif
