# Native ICU i18n. Full data remains separate from the bootstrap-only archive.
include(CMakeFindDependencyMacro)
find_dependency(CHOSICUCommon CONFIG)
if(TARGET CHOS::ICUI18n)
 return()
endif()
get_filename_component(CHOS_ICU_I18N_PREFIX "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
add_library(CHOS::ICUI18n STATIC IMPORTED)
set_target_properties(CHOS::ICUI18n PROPERTIES
 IMPORTED_LOCATION "${CHOS_ICU_I18N_PREFIX}/lib/libicui18n.a"
 INTERFACE_INCLUDE_DIRECTORIES "${CHOS_ICU_I18N_PREFIX}/include"
 INTERFACE_LINK_LIBRARIES CHOS::ICUCommon)
set(CHOSICUI18n_VERSION "79.1")
