# Maintained DER inspection only. No TLS transport or validation-success shim.
get_filename_component(CHOS_CERTIFICATE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CHOS_CERTIFICATE_ROOT}/tools/verify_mbedtls.py"
    WORKING_DIRECTORY "${CHOS_CERTIFICATE_ROOT}" RESULT_VARIABLE _certificate_verified)
if(NOT _certificate_verified EQUAL 0)
    message(FATAL_ERROR "Pinned original certificate parser inventory verification failed")
endif()
set(_cert_mbedtls "${CHOS_CERTIFICATE_ROOT}/third_party/mbedtls")
set(_cert_namespace "${CMAKE_CURRENT_BINARY_DIR}/chos-certificate-namespace.h")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/generate_certificate_namespace.py"
    --source "${_cert_mbedtls}" --out "${_cert_namespace}" RESULT_VARIABLE _certificate_generated)
if(NOT _certificate_generated EQUAL 0)
    message(FATAL_ERROR "Private certificate parser namespace generation failed")
endif()
set(_cert_units asn1parse asn1write bignum bignum_core bignum_mod bignum_mod_raw
    constant_time ecdsa ecp ecp_curves md oid pk pk_ecc pk_wrap pkparse platform_util
    rsa rsa_alt_helpers sha1 sha256 sha512 x509 x509_crt)
set(_cert_sources "${CMAKE_CURRENT_LIST_DIR}/CertificateBackend.c")
foreach(_name IN LISTS _cert_units)
    list(APPEND _cert_sources "${_cert_mbedtls}/library/${_name}.c")
endforeach()
add_library(chos_certificate_parser STATIC ${_cert_sources})
add_library(CHOS::CertificateParser ALIAS chos_certificate_parser)
target_include_directories(chos_certificate_parser PUBLIC "${CMAKE_CURRENT_LIST_DIR}"
    PRIVATE "${_cert_mbedtls}/include" "${_cert_mbedtls}/library")
target_compile_options(chos_certificate_parser PRIVATE "-include" "${_cert_namespace}" -ffunction-sections -fdata-sections)
target_compile_definitions(chos_certificate_parser PRIVATE MBEDTLS_CONFIG_FILE="chos_certificate_config.h" _DEFAULT_SOURCE=1 _POSIX_C_SOURCE=200809L)
set_target_properties(chos_certificate_parser PROPERTIES POSITION_INDEPENDENT_CODE OFF)
