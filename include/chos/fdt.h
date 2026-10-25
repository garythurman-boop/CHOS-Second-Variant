#ifndef CHOS_FDT_H
#define CHOS_FDT_H

#include <stddef.h>
#include <stdint.h>

#define FDT_MAX_MEMORY_REGIONS 32u
#define FDT_MAX_DEVICES 64u
#define FDT_MAX_RESERVED_REGIONS 32u
#define FDT_MODEL_CAPACITY 64u
#define FDT_COMPATIBLE_CAPACITY 96u
#define FDT_NODE_NAME_CAPACITY 48u
#define FDT_BOOTARGS_CAPACITY 128u
#define FDT_STDOUT_PATH_CAPACITY 64u

typedef enum {
    FDT_OK = 0,
    FDT_ERR_NULL_ARGUMENT,
    FDT_ERR_BAD_MAGIC,
    FDT_ERR_BAD_HEADER,
    FDT_ERR_BAD_STRUCTURE,
    FDT_ERR_BAD_RESERVATION_MAP,
    FDT_ERR_TOO_MANY_RESERVATIONS,
    FDT_ERR_UNSUPPORTED_RESERVED_MEMORY_LAYOUT,
    FDT_ERR_TOO_DEEP
} fdt_error_t;

typedef struct {
    uint64_t base;
    uint64_t size;
} fdt_memory_region_t;

typedef struct {
    uint64_t base;
    uint64_t size;
    uint8_t source; /* 1=FDT reserve map, 2=/reserved-memory reg */
} fdt_reserved_region_t;

typedef struct {
    char name[FDT_NODE_NAME_CAPACITY];
    char compatible[FDT_COMPATIBLE_CAPACITY];
    uint64_t reg_base;
    uint64_t reg_size;
    int has_reg;
} fdt_device_t;

typedef struct {
    int valid;
    fdt_error_t error;
    uint32_t total_size;
    char model[FDT_MODEL_CAPACITY];
    char compatible[FDT_COMPATIBLE_CAPACITY];
    char bootargs[FDT_BOOTARGS_CAPACITY];
    char stdout_path[FDT_STDOUT_PATH_CAPACITY];
    fdt_memory_region_t memory[FDT_MAX_MEMORY_REGIONS];
    size_t memory_count;
    fdt_reserved_region_t reserved[FDT_MAX_RESERVED_REGIONS];
    size_t reserved_count;
    size_t reserved_dynamic_count;
    fdt_device_t devices[FDT_MAX_DEVICES];
    size_t device_count;
    int devices_truncated;
    unsigned psci_conduit; /* arm,psci-0.2/1.0 method: 1 SMC, 2 HVC */
} fdt_info_t;

/* Parse an FDT blob in-place without allocation or libc dependencies. */
fdt_error_t fdt_parse_blob(const void *blob, fdt_info_t *out);
const char *fdt_error_string(fdt_error_t error);

#endif
