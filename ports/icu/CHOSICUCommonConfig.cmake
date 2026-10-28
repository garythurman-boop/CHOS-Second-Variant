# Explicit native common-only dependency. The empty upstream bootstrap data
# target is separately named and never substitutes for a complete ICU data API.
if(TARGET CHOS::ICUCommon)
 return()
endif()
get_filename_component(CHOS_ICU_PREFIX "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
add_library(CHOS::ICUCommon STATIC IMPORTED)
set_target_properties(CHOS::ICUCommon PROPERTIES
 IMPORTED_LOCATION "${CHOS_ICU_PREFIX}/lib/libicuuc.a"
 INTERFACE_INCLUDE_DIRECTORIES "${CHOS_ICU_PREFIX}/include"
 INTERFACE_COMPILE_OPTIONS "SHELL:-include \"${CHOS_ICU_PREFIX}/include/chos/icu_native_config.h\"")
add_library(CHOS::ICUBootstrapData STATIC IMPORTED)
set_target_properties(CHOS::ICUBootstrapData PROPERTIES
 IMPORTED_LOCATION "${CHOS_ICU_PREFIX}/lib/libicustubdata.a")
set(CHOSICUCommon_VERSION "79.1")
