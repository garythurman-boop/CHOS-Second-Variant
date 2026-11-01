/* Keep native memory scanning separate from host libc/oracle/interceptors. */
#define sscanf chos_memory_sscanf
#define vsscanf chos_memory_vsscanf
#define __chos_memory_vfscanf chos_memory_vfscanf
#define __chos_memory_intscan chos_memory_intscan
