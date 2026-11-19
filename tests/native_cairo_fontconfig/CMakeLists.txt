cmake_minimum_required(VERSION 3.20)
project(CHOSNativeCairoFontconfig LANGUAGES C)
if(NOT GRAPHICS_PREFIX OR NOT FONT_PREFIX OR NOT FONTCONFIG_PREFIX)
    message(FATAL_ERROR "Provide verified Cairo, FreeType and Fontconfig prefixes")
endif()
find_package(Fontconfig CONFIG REQUIRED PATHS "${FONTCONFIG_PREFIX}/lib/cmake/Fontconfig"
    NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
add_executable(native_cairo_fontconfig main.c)
target_include_directories(native_cairo_fontconfig PRIVATE "${GRAPHICS_PREFIX}/include/cairo")
target_link_libraries(native_cairo_fontconfig PRIVATE
    "${GRAPHICS_PREFIX}/lib/libcairo.a" "${GRAPHICS_PREFIX}/lib/libpixman-1.a"
    Fontconfig::Fontconfig)
if(CMAKE_SYSTEM_NAME STREQUAL "CHOS")
    target_link_options(native_cairo_fontconfig PRIVATE "-Wl,--strip-debug"
        "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/native_cairo_fontconfig.map")
else()
    find_package(Threads REQUIRED)
    target_link_libraries(native_cairo_fontconfig PRIVATE Threads::Threads m)
endif()
