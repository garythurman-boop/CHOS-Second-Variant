#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "chos/process.h"
#include "chos/romfs.h"

int main(void) {
    uint8_t hello[CHOS_EXEC_HEADER_SIZE + 4u] = {0};
    uint8_t about[CHOS_EXEC_HEADER_SIZE + 4u] = {0};
    chos_romfs_file_t files[2];
    chos_romfs_file_t found;
    assert(process_image_write_header(hello, sizeof(hello), 4u, 0u, 1u));
    assert(process_image_write_header(about, sizeof(about), 4u, 0u, 1u));
    /* little-endian AArch64 `b .` as minimal position-independent payload */
    hello[CHOS_EXEC_HEADER_SIZE + 3u] = 0x14u;
    about[CHOS_EXEC_HEADER_SIZE + 3u] = 0x14u;
    files[0].path = "/BIN/HELLO.CHX"; files[0].data = hello; files[0].size = sizeof(hello);
    files[1].path = "/BIN/ABOUT.CHX"; files[1].data = about; files[1].size = sizeof(about);

    assert(romfs_path_is_valid("/BIN/HELLO.CHX"));
    assert(romfs_path_is_valid("/system/ABOUT.CHX"));
    assert(!romfs_path_is_valid("BIN/HELLO.CHX"));
    assert(!romfs_path_is_valid("/BIN/../HELLO.CHX"));
    assert(!romfs_path_is_valid("/BIN//HELLO.CHX"));
    assert(!romfs_path_is_valid("/BIN/HELLO.CHX/"));
    assert(!romfs_path_is_valid("/BIN/HELLO CHX"));
    assert(!romfs_path_is_valid("/BIN/./HELLO.CHX"));
    assert(romfs_path_equal("/bin/hello.chx", "/BIN/HELLO.CHX"));
    assert(!romfs_path_equal("/BIN/HELLO.CHX", "/BIN/ABOUT.CHX"));

    assert(romfs_file_is_valid_chcx(&files[0]));
    assert(romfs_file_is_valid_chcx(&files[1]));
    assert(romfs_find_in_table(files, 2u, "/bin/hello.chx", &found));
    assert(found.data == hello && found.size == sizeof(hello));
    assert(romfs_find_in_table(files, 2u, "/BIN/ABOUT.CHX", &found));
    assert(found.data == about);
    assert(!romfs_find_in_table(files, 2u, "/BIN/MISSING.CHX", &found));
    assert(!romfs_find_in_table(files, 2u, "/BIN/../HELLO.CHX", &found));
    assert(!romfs_find_in_table(files, CHOS_ROMFS_MAX_FILES + 1u, "/BIN/HELLO.CHX", &found));
    files[0].size--;
    assert(!romfs_file_is_valid_chcx(&files[0]));
    puts("ROMFS: absolute-path validation, traversal rejection, case-insensitive lookup and CHCX image validation passed");
    return 0;
}
