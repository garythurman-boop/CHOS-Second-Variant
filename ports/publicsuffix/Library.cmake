get_filename_component(_psl_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(_psl_source "${_psl_root}/third_party/libpsl")
set(_psl_output "${CMAKE_CURRENT_BINARY_DIR}/chos-publicsuffix")
file(MAKE_DIRECTORY "${_psl_output}")
set(LIBPSL_VERSION "0.23.3")
set(LIBPSL_VERSION_MAJOR 0)
set(LIBPSL_VERSION_MINOR 23)
set(LIBPSL_VERSION_PATCH 3)
set(LIBPSL_VERSION_NUMBER 0x001703)
set(COPYRIGHT_YEAR 2026)
configure_file("${_psl_source}/include/libpsl.h.in" "${_psl_output}/libpsl.h" @ONLY)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_custom_command(OUTPUT "${_psl_output}/psl.c" "${_psl_output}/suffixes_dafsa.h"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/prepare.py" "${_psl_output}"
    DEPENDS "${CMAKE_CURRENT_LIST_DIR}/prepare.py" "${CMAKE_CURRENT_LIST_DIR}/builtin-only.patch"
        "${_psl_root}/docs/dependencies/native-publicsuffix.json"
        "${_psl_source}/src/psl.c" "${_psl_source}/src/psl-make-dafsa"
        "${_psl_root}/third_party/publicsuffix/public_suffix_list.dat"
    VERBATIM)
add_library(chos_publicsuffix STATIC "${_psl_output}/psl.c" "${_psl_source}/src/lookup_string_in_fixed_set.c")
target_include_directories(chos_publicsuffix PUBLIC "${_psl_output}")
target_compile_definitions(chos_publicsuffix PRIVATE BUILDING_PSL=1 ENABLE_BUILTIN=1 PSL_BUILTIN_ONLY=1 PACKAGE_VERSION="0.23.3")
target_compile_options(chos_publicsuffix PRIVATE -UHAVE_CONFIG_H -ffunction-sections -fdata-sections)
