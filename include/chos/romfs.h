#ifndef CHOS_ROMFS_H
#define CHOS_ROMFS_H

#include <stddef.h>
#include <stdint.h>

#define CHOS_ROMFS_MAX_FILES 12u
#define CHOS_ROMFS_PATH_CAPACITY 64u

typedef struct {
    const char *path;
    const uint8_t *data;
    size_t size;
} chos_romfs_file_t;

/* Read-only packaged-file catalog. Paths are absolute and case-insensitive. */
size_t romfs_file_count(void);
int romfs_get_at(size_t index, chos_romfs_file_t *out);
int romfs_lookup(const char *path, chos_romfs_file_t *out);
int romfs_validate_all(void);

/* Pure helpers shared by firmware and host tests. */
int romfs_path_is_valid(const char *path);
int romfs_path_equal(const char *left, const char *right);
int romfs_find_in_table(const chos_romfs_file_t *files, size_t count,
                        const char *path, chos_romfs_file_t *out);
int romfs_file_is_valid_chcx(const chos_romfs_file_t *file);

#endif
