# The actual default resource package, compiled for AArch64. This is distinct
# from the explicitly named empty bootstrap archive used by small fixtures.
if(TARGET CHOS::ICUData)
 return()
endif()
get_filename_component(_chos_icu_data_prefix "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
add_library(CHOS::ICUData STATIC IMPORTED)
set_target_properties(CHOS::ICUData PROPERTIES
 IMPORTED_LOCATION "${_chos_icu_data_prefix}/lib/libicudata.a")
set(CHOSICUData_VERSION "79.1")
unset(_chos_icu_data_prefix)
