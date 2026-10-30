cmake_minimum_required(VERSION 3.20)
project(CHOSWTFFileHandles LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_executable(wtf_file_handles main.cpp
 "${WEBKIT_SOURCE}/Source/WTF/wtf/FileHandle.cpp"
 "${WEBKIT_SOURCE}/Source/WTF/wtf/posix/MappedFileDataPOSIX.cpp"
 "${CMAKE_CURRENT_LIST_DIR}/../../ports/webkit/chos/FileHandleCHOS.cpp")
target_include_directories(wtf_file_handles PRIVATE "${ENGINE_BUILD}" "${WEBKIT_SOURCE}/Source/WTF" "${WEBKIT_SOURCE}/Source/bmalloc")
target_compile_definitions(wtf_file_handles PRIVATE BUILDING_JSCONLY__ BUILDING_WITH_CMAKE=1 HAVE_CONFIG_H=1 BUILDING_WTF=1 NDEBUG=1)
