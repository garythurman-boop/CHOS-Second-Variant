cmake_minimum_required(VERSION 3.20)
project(CHOSNativeLargeHeap LANGUAGES C)
add_executable(native_large_heap main.c)
target_link_options(native_large_heap PRIVATE "-Wl,--strip-debug")
