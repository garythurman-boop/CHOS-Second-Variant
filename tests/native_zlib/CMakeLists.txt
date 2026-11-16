cmake_minimum_required(VERSION 3.20)
project(CHOSNativeZlib LANGUAGES C)
if(NOT ZLIB_PREFIX)
    message(FATAL_ERROR "Provide the verified memory API ZLIB_PREFIX")
endif()
find_package(ZLIB REQUIRED CONFIG PATHS "${ZLIB_PREFIX}/lib/cmake/ZLIB"
    NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
include(CheckCSourceCompiles)
set(CMAKE_REQUIRED_LIBRARIES ZLIB::ZLIB)
check_c_source_compiles("#include <zlib.h>
int main(void) {
    z_stream stream = {0}; unsigned char input[1] = {0}, output[64];
    uLongf size = sizeof(output); uLong consumed = 1;
    int status = deflateInit(&stream, 6);
    if (status == Z_OK) status += deflateEnd(&stream);
    status += compress(output, &size, input, 1);
    size = sizeof(output); status += uncompress2(output, &size, input, &consumed);
    return status;
}" CHOS_ZLIB_MEMORY_APIS_LINK)
if(NOT CHOS_ZLIB_MEMORY_APIS_LINK)
    message(FATAL_ERROR "The memory API package did not link through ZLIB::ZLIB")
endif()
# Public original headers still document upstream's complete API. This profile
# intentionally supplies no gzFile symbols. Require an actual unresolved-symbol
# failure, so a missing header or broken compiler cannot masquerade as evidence.
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/gzfile-probe.c"
    "#include <zlib.h>\nint main(void) { gzFile f = gzopen(\"absent\", \"rb\"); return gzclose(f); }\n")
try_compile(CHOS_ZLIB_GZFILE_LINKED "${CMAKE_CURRENT_BINARY_DIR}/gzfile-probe"
    SOURCES "${CMAKE_CURRENT_BINARY_DIR}/gzfile-probe.c"
    LINK_LIBRARIES ZLIB::ZLIB OUTPUT_VARIABLE gzfile_probe)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/gzfile-probe.log" "${gzfile_probe}")
if(CHOS_ZLIB_GZFILE_LINKED OR NOT gzfile_probe MATCHES "undefined" OR NOT gzfile_probe MATCHES "gzopen")
    message(FATAL_ERROR "Expected unresolved gzopen/gzclose evidence: ${gzfile_probe}")
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/generate_cases.py"
    --out "${CMAKE_CURRENT_BINARY_DIR}/cases"
    RESULT_VARIABLE cases_result)
if(NOT cases_result EQUAL 0)
    message(FATAL_ERROR "Independent host zlib fixture generation failed")
endif()
add_executable(native_zlib main.c)
target_include_directories(native_zlib PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/cases")
target_link_libraries(native_zlib PRIVATE ZLIB::ZLIB)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "CHOS")
    find_package(Threads REQUIRED)
    target_link_libraries(native_zlib PRIVATE Threads::Threads)
else()
    target_link_options(native_zlib PRIVATE "-Wl,--strip-debug")
endif()
