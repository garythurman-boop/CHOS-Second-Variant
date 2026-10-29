cmake_minimum_required(VERSION 3.20)
project(CHOSWTFPrimitives LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT EXISTS "${WEBKIT_SOURCE}/Source/WTF/wtf/ThreadingPrimitives.h" OR NOT EXISTS "${ENGINE_BUILD}/cmakeconfig.h")
 message(FATAL_ERROR "Use the checked upstream source and real JSCOnly configuration")
endif()
add_executable(wtf_primitives main.cpp ../../ports/webkit/chos/ThreadingPrimitivesCHOS.cpp
 "${WEBKIT_SOURCE}/Source/WTF/wtf/CurrentTime.cpp"
 "${WEBKIT_SOURCE}/Source/WTF/wtf/WallTime.cpp"
 "${WEBKIT_SOURCE}/Source/WTF/wtf/MonotonicTime.cpp")
target_include_directories(wtf_primitives PRIVATE "${ENGINE_BUILD}" "${WEBKIT_SOURCE}/Source/WTF" "${WEBKIT_SOURCE}/Source/bmalloc")
target_compile_definitions(wtf_primitives PRIVATE BUILDING_JSCONLY__ BUILDING_WITH_CMAKE=1 HAVE_CONFIG_H=1 BUILDING_WTF=1 NDEBUG=1)
