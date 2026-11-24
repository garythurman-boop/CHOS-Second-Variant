# Real maintained EC/RSA parsing, ownership and DER writing. Separate namespace
# and configuration from native TLS and certificate inspection.
get_filename_component(CHOS_KEY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/verify_sources.py"
    WORKING_DIRECTORY "${CHOS_KEY_ROOT}" RESULT_VARIABLE _key_verified)
if(NOT _key_verified EQUAL 0)
    message(FATAL_ERROR "Pinned maintained key source verification failed")
endif()
set(_key_mbedtls "${CHOS_KEY_ROOT}/third_party/mbedtls")
set(_key_extra "${CHOS_KEY_ROOT}/third_party/mbedtls-webcore-keys")
set(_key_namespace "${CMAKE_CURRENT_BINARY_DIR}/chos-key-namespace.h")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/generate_namespace.py"
    --source "${_key_mbedtls}" --source "${_key_extra}" --out "${_key_namespace}"
    RESULT_VARIABLE _key_generated)
if(NOT _key_generated EQUAL 0)
    message(FATAL_ERROR "Private key namespace generation failed")
endif()
set(_key_units asn1parse asn1write bignum bignum_core bignum_mod bignum_mod_raw
    constant_time ecdsa ecp ecp_curves md oid pk pk_ecc pk_wrap pkparse platform_util
    rsa rsa_alt_helpers sha1 sha256 sha512)
set(_key_sources "${CMAKE_CURRENT_LIST_DIR}/KeyBackend.c" "${_key_extra}/library/pkwrite.c")
foreach(_name IN LISTS _key_units)
    list(APPEND _key_sources "${_key_mbedtls}/library/${_name}.c")
endforeach()
add_library(chos_webcore_keys STATIC ${_key_sources})
if(CMAKE_SYSTEM_NAME STREQUAL "CHOS")
    target_sources(chos_webcore_keys PRIVATE "${CMAKE_CURRENT_LIST_DIR}/KeyEntropyCHOS.c")
endif()
add_library(CHOS::CryptoKeys ALIAS chos_webcore_keys)
target_include_directories(chos_webcore_keys PUBLIC "${CMAKE_CURRENT_LIST_DIR}"
    PRIVATE "${_key_mbedtls}/include" "${_key_mbedtls}/library")
target_compile_options(chos_webcore_keys PRIVATE -include "${_key_namespace}" -ffunction-sections -fdata-sections)
target_compile_definitions(chos_webcore_keys PRIVATE MBEDTLS_CONFIG_FILE="chos_key_config.h")
set_target_properties(chos_webcore_keys PROPERTIES POSITION_INDEPENDENT_CODE OFF)
