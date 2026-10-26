# Dependency triage only, not the final engine toolchain or a link-support claim.
get_filename_component(CHOS_SOURCE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(CHOS_BUILD_ROOT "${CHOS_SOURCE_ROOT}/build")
list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")
set(CMAKE_SYSTEM_NAME CHOS)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_C_COMPILER_TARGET aarch64-none-elf)
set(CMAKE_CXX_COMPILER_TARGET aarch64-none-elf)
set(CMAKE_AR llvm-ar)
set(CMAKE_RANLIB llvm-ranlib)
# The existing SDK has no general hosted executable link/loader profile yet.
# Therefore CMake library-existence and thread checks are NOT link evidence.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_C_FLAGS_INIT "-ffreestanding -fno-builtin -fno-stack-protector -femulated-tls -I\"${CHOS_SOURCE_ROOT}/userland/libc/include\" -I\"${CHOS_SOURCE_ROOT}/userland/libchos/include\" -I\"${CHOS_SOURCE_ROOT}/include\" -I\"${CHOS_BUILD_ROOT}/userland/libm/include\"")
set(CMAKE_CXX_FLAGS_INIT "-ffreestanding -fno-builtin -fno-stack-protector -femulated-tls -fno-exceptions -fno-rtti -nostdinc++ -isystem \"${CHOS_BUILD_ROOT}/userland/libcxx/include\" -isystem \"${CHOS_SOURCE_ROOT}/userland/libcxx/include\" -isystem \"${CHOS_SOURCE_ROOT}/third_party/libcxx/include\" -isystem \"${CHOS_SOURCE_ROOT}/userland/libc/include\" -isystem \"${CHOS_BUILD_ROOT}/userland/libm/include\" -I\"${CHOS_SOURCE_ROOT}/userland/libchos/include\" -I\"${CHOS_SOURCE_ROOT}/include\"")
# Never let a host ICU/pkg-config library satisfy a CHOS dependency.
set(CMAKE_FIND_ROOT_PATH "${CHOS_BUILD_ROOT}/jsc-sysroot")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
