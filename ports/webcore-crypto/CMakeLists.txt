# Digest-only CHOS PAL dependency. The CryptoDigest C++ adapter belongs to the
# actual PAL target and must use that target's generated WebCore configuration.
cmake_minimum_required(VERSION 3.20)
get_filename_component(CHOS_DIGEST_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${CHOS_DIGEST_ROOT}/tools/verify_mbedtls.py"
    WORKING_DIRECTORY "${CHOS_DIGEST_ROOT}"
    RESULT_VARIABLE CHOS_DIGEST_VERIFY_RESULT
)
if(NOT CHOS_DIGEST_VERIFY_RESULT EQUAL 0)
    message(FATAL_ERROR "Original pinned MbedTLS source verification failed")
endif()
set(CHOS_DIGEST_MBEDTLS "${CHOS_DIGEST_ROOT}/third_party/mbedtls")
add_library(chos_pal_digest STATIC
    DigestBackend.c
    "${CHOS_DIGEST_MBEDTLS}/library/sha1.c"
    "${CHOS_DIGEST_MBEDTLS}/library/sha256.c"
    "${CHOS_DIGEST_MBEDTLS}/library/sha512.c"
    "${CHOS_DIGEST_MBEDTLS}/library/platform_util.c"
)
add_library(CHOS::PALDigest ALIAS chos_pal_digest)
target_compile_definitions(chos_pal_digest PRIVATE MBEDTLS_CONFIG_FILE="chos_digest_config.h")
target_include_directories(chos_pal_digest PUBLIC "${CMAKE_CURRENT_LIST_DIR}"
    PRIVATE "${CHOS_DIGEST_MBEDTLS}/include" "${CHOS_DIGEST_MBEDTLS}/library")
set_target_properties(chos_pal_digest PROPERTIES POSITION_INDEPENDENT_CODE OFF)
