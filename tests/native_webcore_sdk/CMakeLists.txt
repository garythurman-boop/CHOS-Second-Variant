cmake_minimum_required(VERSION 3.20)
project(CHOSWebCoreSDK LANGUAGES CXX)
add_executable(native_webcore_sdk main.cpp)
target_compile_features(native_webcore_sdk PRIVATE cxx_std_23)
