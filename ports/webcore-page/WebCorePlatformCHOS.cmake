# Proposed native software source selection; no platform service stubs.
include(platform/Cairo.cmake)
# Several generated bindings include this header before compiling DOM objects.
add_custom_target(CHOSWebCoreNames DEPENDS "${WebCore_DERIVED_SOURCES_DIR}/XLinkNames.h")
list(APPEND WebCore_DEPENDENCIES CHOSWebCoreNames)
get_filename_component(_cairo_libdir "${Cairo_LIBRARY}" DIRECTORY)
find_library(CHOS_PIXMAN_LIBRARY NAMES pixman-1 PATHS "${_cairo_libdir}" NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH REQUIRED)
list(APPEND WebCore_LIBRARIES "${CHOS_PIXMAN_LIBRARY}")
include(platform/FreeType.cmake)
include(platform/ImageDecoders.cmake)
if(CHOS_CXX_STANDARD_HEADER_OVERLAY)
    if(NOT EXISTS "${CHOS_CXX_STANDARD_HEADERS_INVENTORY}")
        message(FATAL_ERROR "Provide the verified maintained C++ header inventory")
    endif()
    file(READ "${CHOS_CXX_STANDARD_HEADERS_INVENTORY}" _chos_cpp_inventory)
    string(JSON _chos_cpp_version GET "${_chos_cpp_inventory}" version)
    string(JSON _chos_cpp_commit GET "${_chos_cpp_inventory}" commit)
    if(NOT _chos_cpp_version STREQUAL "19.1.7" OR NOT _chos_cpp_commit STREQUAL "cd708029e0b2869e80abe31ddb175f7c35361f90")
        message(FATAL_ERROR "The C++ overlay must use the pinned LLVM19.1.7 inventory")
    endif()
    file(GLOB_RECURSE _chos_overlay_files LIST_DIRECTORIES false RELATIVE "${CHOS_CXX_STANDARD_HEADER_OVERLAY}" "${CHOS_CXX_STANDARD_HEADER_OVERLAY}/*")
    list(LENGTH _chos_overlay_files _chos_overlay_count)
    if(_chos_overlay_count LESS 1)
        message(FATAL_ERROR "The explicit maintained standard-header overlay must not be empty")
    endif()
    foreach(_chos_header IN LISTS _chos_overlay_files)
        if(IS_SYMLINK "${CHOS_CXX_STANDARD_HEADER_OVERLAY}/${_chos_header}" OR (_chos_header MATCHES "\\.h$" AND NOT _chos_header MATCHES "^__"))
            message(FATAL_ERROR "C wrappers and symlinks are forbidden in the C++ overlay: ${_chos_header}")
        endif()
        string(JSON _chos_header_expected ERROR_VARIABLE _chos_header_error GET "${_chos_cpp_inventory}" files "include/${_chos_header}")
        file(SHA256 "${CHOS_CXX_STANDARD_HEADER_OVERLAY}/${_chos_header}" _chos_header_sha)
        if(_chos_header_error OR NOT _chos_header_sha STREQUAL _chos_header_expected)
            message(FATAL_ERROR "Maintained C++ header differs from the pinned original: ${_chos_header}")
        endif()
    endforeach()
    # All explicit headers are maintained originals. Top-level C wrappers cannot
    # intercept include_next; no immutable SDK or host include path is modified.
    list(PREPEND WebCore_PRIVATE_INCLUDE_DIRECTORIES "${CHOS_CXX_STANDARD_HEADER_OVERLAY}")
endif()
list(APPEND WebCore_PRIVATE_INCLUDE_DIRECTORIES "${WEBCORE_DIR}/platform/chos")
list(APPEND WebCore_SOURCES platform/chos/SharedMemoryCHOS.cpp)

# These value classes own the original common request/response/error state;
# selecting them does not install a resource loader or claim network I/O.
if(NOT IS_DIRECTORY "${CHOS_WEBCORE_RESOURCE_TYPES_DIR}")
    message(FATAL_ERROR "Provide the genuine CHOS resource value/certificate port")
endif()
list(APPEND WebCore_PRIVATE_INCLUDE_DIRECTORIES "${CHOS_WEBCORE_RESOURCE_TYPES_DIR}")
foreach(_header ResourceRequest ResourceResponse ResourceError CertificateInfo AuthenticationChallenge)
    list(APPEND WebCore_PRIVATE_FRAMEWORK_HEADERS "${CHOS_WEBCORE_RESOURCE_TYPES_DIR}/${_header}.h")
endforeach()
foreach(_source ResourceRequest ResourceResponse ResourceError CertificateInfo CertificatePersistence)
    list(APPEND WebCore_SOURCES "${CHOS_WEBCORE_RESOURCE_TYPES_DIR}/${_source}CHOS.cpp")
endforeach()
if(NOT TARGET CHOS::CertificateParser)
    include("${CHOS_WEBCORE_RESOURCE_TYPES_DIR}/CertificateParser.cmake")
endif()
list(APPEND WebCore_LIBRARIES CHOS::CertificateParser)

# Mandatory WebCrypto key classes retain actual private MbedTLS key contexts.
# Providing key ownership does not claim the remaining WebCrypto algorithms.
if(NOT IS_DIRECTORY "${CHOS_WEBCORE_CRYPTO_KEYS_DIR}")
    message(FATAL_ERROR "Provide the genuine CHOS EC/RSA key ownership backend")
endif()
list(APPEND WebCore_PRIVATE_INCLUDE_DIRECTORIES "${CHOS_WEBCORE_CRYPTO_KEYS_DIR}")
if(NOT TARGET CHOS::CryptoKeys)
    include("${CHOS_WEBCORE_CRYPTO_KEYS_DIR}/Keys.cmake")
endif()
list(APPEND WebCore_LIBRARIES CHOS::CryptoKeys)
list(APPEND WebCore_SOURCES "${CHOS_WEBCORE_CRYPTO_KEYS_DIR}/CryptoKeyDERCHOS.cpp"
    "${CHOS_WEBCORE_CRYPTO_KEYS_DIR}/CryptoAlgorithmRegistryCHOS.cpp")

if(NOT IS_DIRECTORY "${CHOS_WEBCORE_PLATFORM_DIR}")
    message(FATAL_ERROR "Provide the actual CHOS software platform policy sources")
endif()
list(APPEND WebCore_PRIVATE_INCLUDE_DIRECTORIES "${CHOS_WEBCORE_PLATFORM_DIR}")
foreach(_source Theme ScrollbarTheme GraphicsLayer ResourceHandle SessionCookieStore NetworkStorageSession Pasteboard EditorFont PublicSuffixStore DNSResolveQueue SystemFontDatabase MIMETypeRegistry AXObjectCache AccessibilityObject)
    list(APPEND WebCore_SOURCES "${CHOS_WEBCORE_PLATFORM_DIR}/${_source}CHOS.cpp")
endforeach()
include("${CHOS_WEBCORE_PLATFORM_DIR}/../publicsuffix/Library.cmake")
set_source_files_properties("${CHOS_WEBCORE_PLATFORM_DIR}/PublicSuffixStoreCHOS.cpp"
    PROPERTIES INCLUDE_DIRECTORIES "$<TARGET_PROPERTY:chos_publicsuffix,INTERFACE_INCLUDE_DIRECTORIES>")
list(APPEND WebCore_LIBRARIES "$<LINK_ONLY:chos_publicsuffix>")
list(APPEND WebCore_DEPENDENCIES chos_publicsuffix)
list(APPEND WebCore_SOURCES platform/text/LocaleICU.cpp platform/text/Hyphenation.cpp
    editing/glib/WebContentReaderGLib.cpp editing/glib/EditorGLib.cpp)
if(EXISTS "${CHOS_SDK_ROOT}/include/chos/gui_screen.h")
    list(APPEND WebCore_SOURCES "${CHOS_WEBCORE_PLATFORM_DIR}/PlatformScreenCHOS.cpp")
else()
    message(STATUS "CHOS GUI screen snapshot header is absent; screen backend remains unlinked")
endif()
# Input/resource-loading services remain separate required backends.

# Static WebCore propagates bindings, DOM/rendering, and style object libraries
# via its link interface. The diagnostic consumes that same interface.
add_executable(CHOSLocalPage EXCLUDE_FROM_ALL "${WEBCORE_DIR}/platform/chos/LocalPageDiagnostic.cpp"
    "${CHOS_WEBCORE_PLATFORM_DIR}/../../tests/native_webcore_page/LocalPlatformTests.cpp"
    "${CHOS_WEBCORE_PLATFORM_DIR}/../../tests/native_webcore_page/LocalPageStrategies.cpp")
add_subdirectory("${CHOS_WEBCORE_PLATFORM_DIR}/../icu/browser-data" "${CMAKE_CURRENT_BINARY_DIR}/chos-browser-icu")
target_include_directories(CHOSLocalPage PRIVATE "${CHOS_WEBCORE_PLATFORM_DIR}/../../tests/native_webcore_page")
target_link_libraries(CHOSLocalPage PRIVATE WebCore chos_browser_icu_data)
# ICU resolves individual resources from the verified packages registered at
# startup. Its upstream bootstrap object supplies the required default symbol.
target_link_options(CHOSLocalPage PRIVATE "-Wl,--icf=safe" "-Wl,--wrap=abort"
    "-Wl,--whole-archive,${CHOS_SDK_ROOT}/lib/libicustubdata.a,--no-whole-archive")
target_include_directories(CHOSLocalPage PRIVATE "$<TARGET_PROPERTY:WebCore,INCLUDE_DIRECTORIES>")
target_compile_definitions(CHOSLocalPage PRIVATE "$<TARGET_PROPERTY:WebCore,COMPILE_DEFINITIONS>")
target_link_options(CHOSLocalPage PRIVATE "-Wl,--strip-debug" "-Wl,-Map,${CMAKE_BINARY_DIR}/CHOSLocalPage.map")
add_subdirectory("${WEBCORE_DIR}/platform/chos/network-values" "${CMAKE_CURRENT_BINARY_DIR}/chos-network-values")
add_subdirectory("${CHOS_WEBCORE_PLATFORM_DIR}/../../tests/native_webcore_clipboard" "${CMAKE_CURRENT_BINARY_DIR}/chos-clipboard-values")
add_subdirectory("${CHOS_WEBCORE_PLATFORM_DIR}/../../tests/native_webcore_cookies" "${CMAKE_CURRENT_BINARY_DIR}/chos-cookie-values")
