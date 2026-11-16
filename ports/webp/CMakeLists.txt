cmake_minimum_required(VERSION 3.20)
project(CHOSWebP C)

if(NOT IS_DIRECTORY "${WEBP_SOURCE}/src")
    message(FATAL_ERROR "WEBP_SOURCE must name the verified original libwebp source")
endif()

set(BUILD_SHARED_LIBS OFF CACHE BOOL "Static native dependency" FORCE)
set(WEBP_LINK_STATIC ON CACHE BOOL "Static native dependency" FORCE)
set(WEBP_ENABLE_SIMD ON CACHE BOOL "Retain actual compiler SIMD probes" FORCE)
set(WEBP_USE_THREAD ON CACHE BOOL "Retain real thread implementation" FORCE)
foreach(option ANIM_UTILS CWEBP DWEBP GIF2WEBP IMG2WEBP VWEBP WEBPINFO
               LIBWEBPMUX WEBPMUX EXTRAS WEBP_JS FUZZTEST)
    set(WEBP_BUILD_${option} OFF CACHE BOOL "Memory library profile" FORCE)
endforeach()

add_subdirectory("${WEBP_SOURCE}" upstream)

if(CMAKE_SYSTEM_NAME STREQUAL "CHOS")
    if(NOT CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
        message(FATAL_ERROR "This native WebP profile requires AArch64")
    endif()
    # Upstream defaults static libraries to PIC. CHOS links an owned static
    # executable; this is a code-generation choice, not a feature-probe result.
    foreach(target sharpyuv webpdecode webpdspdecode webputilsdecode
                   webpdecoder webpencode webpdsp webputils webp webpdemux)
        set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE OFF)
    endforeach()
    file(READ "${CMAKE_CURRENT_BINARY_DIR}/upstream/src/webp/config.h" webp_config)
    if(NOT webp_config MATCHES "#define WEBP_HAVE_NEON[\r\n]")
        message(FATAL_ERROR "Actual upstream AArch64 NEON compiler probe failed")
    endif()
    if(NOT webp_config MATCHES "#define WEBP_USE_THREAD 1")
        message(FATAL_ERROR "Actual upstream Threads probe failed")
    endif()
endif()
