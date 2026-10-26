# Compiler-only bring-up profile. No Linux/POSIX platform inheritance.
set_property(GLOBAL PROPERTY TARGET_SUPPORTS_SHARED_LIBS FALSE)
set(CMAKE_SYSTEM_INCLUDE_PATH /include)
set(CMAKE_SYSTEM_LIBRARY_PATH /lib)
set(CMAKE_SYSTEM_PROGRAM_PATH /bin)
