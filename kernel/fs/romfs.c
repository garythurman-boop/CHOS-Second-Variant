#include <stddef.h>
#include <stdint.h>
#include "chos/romfs.h"

typedef struct {
    const char *path;
    const uint8_t *start;
    const uint8_t *end;
} embedded_file_t;

extern const uint8_t chos_romfs_hello_start[];
extern const uint8_t chos_romfs_hello_end[];
extern const uint8_t chos_romfs_about_start[];
extern const uint8_t chos_romfs_about_end[];
extern const uint8_t chos_romfs_fault_start[];
extern const uint8_t chos_romfs_fault_end[];
extern const uint8_t chos_romfs_async_start[];
extern const uint8_t chos_romfs_async_end[];
extern const uint8_t chos_romfs_cdemo_start[];
extern const uint8_t chos_romfs_cdemo_end[];
extern const uint8_t chos_romfs_guiapp_start[];
extern const uint8_t chos_romfs_guiapp_end[];
extern const uint8_t chos_romfs_textview_start[];
extern const uint8_t chos_romfs_textview_end[];
extern const uint8_t chos_romfs_console_start[];
extern const uint8_t chos_romfs_console_end[];
extern const uint8_t chos_romfs_fileman_start[];
extern const uint8_t chos_romfs_fileman_end[];
extern const uint8_t chos_romfs_browser_start[];
extern const uint8_t chos_romfs_browser_end[];

static const embedded_file_t embedded_files[] = {
    { "/BIN/HELLO.CHX", chos_romfs_hello_start, chos_romfs_hello_end },
    { "/BIN/ABOUT.CHX", chos_romfs_about_start, chos_romfs_about_end },
    { "/BIN/FAULT.CHX", chos_romfs_fault_start, chos_romfs_fault_end },
    { "/BIN/ASYNC.CHX", chos_romfs_async_start, chos_romfs_async_end },
    { "/BIN/CDEMO.CHX", chos_romfs_cdemo_start, chos_romfs_cdemo_end },
    { "/BIN/GUIAPP.CHX", chos_romfs_guiapp_start, chos_romfs_guiapp_end },
    { "/BIN/TEXTVIEW.CHX", chos_romfs_textview_start, chos_romfs_textview_end },
    { "/BIN/CONSOLE.CHX", chos_romfs_console_start, chos_romfs_console_end },
    { "/BIN/FILEMAN.CHX", chos_romfs_fileman_start, chos_romfs_fileman_end },
    { "/BIN/BROWSER.CHX", chos_romfs_browser_start, chos_romfs_browser_end }
};

size_t romfs_file_count(void) {
    return sizeof(embedded_files) / sizeof(embedded_files[0]);
}

int romfs_get_at(size_t index, chos_romfs_file_t *out) {
    uintptr_t begin, end;
    if (out == NULL || index >= romfs_file_count()) return 0;
    begin = (uintptr_t)embedded_files[index].start;
    end = (uintptr_t)embedded_files[index].end;
    if (end <= begin) return 0;
    out->path = embedded_files[index].path;
    out->data = embedded_files[index].start;
    out->size = (size_t)(end - begin);
    return 1;
}

int romfs_lookup(const char *path, chos_romfs_file_t *out) {
    chos_romfs_file_t table[CHOS_ROMFS_MAX_FILES];
    size_t i, count = romfs_file_count();
    if (count > CHOS_ROMFS_MAX_FILES || out == NULL || !romfs_path_is_valid(path)) return 0;
    for (i = 0u; i < count; ++i) {
        if (!romfs_get_at(i, &table[i])) return 0;
    }
    return romfs_find_in_table(table, count, path, out);
}

int romfs_validate_all(void) {
    chos_romfs_file_t file, other;
    size_t i, j;
    for (i = 0u; i < romfs_file_count(); ++i) {
        if (!romfs_get_at(i, &file) || !romfs_file_is_valid_chcx(&file)) return 0;
        for (j = 0u; j < i; ++j) {
            if (!romfs_get_at(j, &other) || romfs_path_equal(file.path, other.path)) return 0;
        }
    }
    return 1;
}
