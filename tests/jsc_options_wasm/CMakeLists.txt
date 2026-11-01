cmake_minimum_required(VERSION 3.20)
project(CHOSJSCOptionsWasm LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_executable(jsc_options_wasm main.cpp)
target_include_directories(jsc_options_wasm PRIVATE "${ENGINE_BUILD}" "${ENGINE_BUILD}/JavaScriptCore/PrivateHeaders" "${ENGINE_BUILD}/JavaScriptCore/PrivateHeaders/JavaScriptCore" "${ENGINE_BUILD}/JavaScriptCore/DerivedSources" "${WEBKIT_SOURCE}/Source/JavaScriptCore" "${WEBKIT_SOURCE}/Source/WTF" "${WEBKIT_SOURCE}/Source/bmalloc")
target_compile_definitions(jsc_options_wasm PRIVATE BUILDING_JSCONLY__ BUILDING_WITH_CMAKE=1 HAVE_CONFIG_H=1 BUILDING_WTF=1 NDEBUG=1)
# Only the options/platform component is executed. The bootstrap ICU archive
# does not qualify full ICU data, a VM, WebAssembly execution or Intl behavior.
target_link_libraries(jsc_options_wasm PRIVATE "-Wl,--start-group" "${ENGINE_BUILD}/lib/libJavaScriptCore.a" "${ENGINE_BUILD}/lib/libWTF.a" "${ENGINE_BUILD}/lib/libbmalloc.a" "${CHOS_SDK_ROOT}/lib/libicuuc.a" "${CHOS_SDK_ROOT}/lib/libicustubdata.a" "-Wl,--end-group")
