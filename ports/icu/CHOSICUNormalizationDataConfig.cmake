# Explicit selected resources, not the full ICU "data" library component.
get_filename_component(_chos_icu_data_prefix "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(CHOS_ICU_NORMALIZATION_DATA_DIRECTORY "${_chos_icu_data_prefix}/share/icu-normalization")
set(CHOS_ICU_NORMALIZATION_DATA_FILES)
foreach(_chos_icu_resource IN ITEMS nfkc nfkc_cf nfkc_scf uts46)
 set(_chos_icu_path "${CHOS_ICU_NORMALIZATION_DATA_DIRECTORY}/icudt79l/${_chos_icu_resource}.nrm")
 if(NOT EXISTS "${_chos_icu_path}")
  message(FATAL_ERROR "Missing selected ICU normalization resource: ${_chos_icu_path}")
 endif()
 list(APPEND CHOS_ICU_NORMALIZATION_DATA_FILES "${_chos_icu_path}")
endforeach()
set(CHOSICUNormalizationData_VERSION "79.1")
set(CHOS_ICU_NORMALIZATION_UNICODE_VERSION "18.0.0")
# Deploy the directory to the guest, then explicitly set its guest path using
# u_setDataDirectory before starting ICU workers. Host SDK paths are not valid
# guest paths. No empty bootstrap library is renamed or exposed as libicudata.
unset(_chos_icu_resource)
unset(_chos_icu_path)
unset(_chos_icu_data_prefix)
