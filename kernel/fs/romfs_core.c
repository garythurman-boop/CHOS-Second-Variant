#include <stddef.h>
#include <stdint.h>
#include "chos/process.h"
#include "chos/romfs.h"

static int ascii_lower(int ch) {
    if (ch >= 'A' && ch <= 'Z') return ch + ('a' - 'A');
    return ch;
}

int romfs_path_is_valid(const char *path) {
    size_t length = 0u;
    size_t component_length = 0u;
    size_t i;
    if (path == NULL || path[0] != '/') return 0;
    while (path[length] != '\0') {
        if (++length >= CHOS_ROMFS_PATH_CAPACITY) return 0;
    }
    if (length < 2u || path[length - 1u] == '/') return 0;
    for (i = 1u; i <= length; ++i) {
        int ch = (i == length) ? '/' : (unsigned char)path[i];
        if (ch == '/') {
            if (component_length == 0u) return 0;
            if (component_length == 1u && path[i - 1u] == '.') return 0;
            if (component_length == 2u && path[i - 1u] == '.' && path[i - 2u] == '.') return 0;
            component_length = 0u;
        } else {
            if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                  (ch >= '0' && ch <= '9') || ch == '_' || ch == '-' || ch == '.')) return 0;
            ++component_length;
        }
    }
    return 1;
}

int romfs_path_equal(const char *left, const char *right) {
    size_t i = 0u;
    if (!romfs_path_is_valid(left) || !romfs_path_is_valid(right)) return 0;
    while (left[i] != '\0' && right[i] != '\0') {
        if (ascii_lower((unsigned char)left[i]) != ascii_lower((unsigned char)right[i])) return 0;
        ++i;
    }
    return left[i] == '\0' && right[i] == '\0';
}

int romfs_file_is_valid_chcx(const chos_romfs_file_t *file) {
    chos_exec_image_info_t info;
    if (file == NULL || !romfs_path_is_valid(file->path) || file->data == NULL || file->size == 0u)
        return 0;
    return process_image_validate(file->data, file->size, &info);
}

int romfs_find_in_table(const chos_romfs_file_t *files, size_t count,
                        const char *path, chos_romfs_file_t *out) {
    size_t i;
    if (files == NULL || path == NULL || out == NULL || !romfs_path_is_valid(path) ||
        count > CHOS_ROMFS_MAX_FILES) return 0;
    for (i = 0u; i < count; ++i) {
        if (romfs_path_equal(files[i].path, path)) {
            if (!romfs_file_is_valid_chcx(&files[i])) return 0;
            *out = files[i];
            return 1;
        }
    }
    return 0;
}
