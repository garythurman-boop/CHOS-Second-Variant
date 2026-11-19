cmake_minimum_required(VERSION 3.20)
project(CHOSNativeGUIScreen LANGUAGES C)
add_executable(native_gui_screen main.c)
target_compile_options(native_gui_screen PRIVATE -fno-builtin)
