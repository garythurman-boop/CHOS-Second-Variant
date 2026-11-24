# Compile the original implementations with the same generated ABI and headers
# as the enclosing full WebCore build. This smaller consumer can run on its own.
set(_value_root "${WEBCORE_DIR}/platform/network")
add_executable(CHOSNetworkValues EXCLUDE_FROM_ALL
    "${WEBCORE_DIR}/platform/chos/NetworkValueDiagnostic.cpp"
    "${_value_root}/ResourceRequestBase.cpp" "${_value_root}/ResourceResponseBase.cpp"
    "${_value_root}/ResourceErrorBase.cpp" "${_value_root}/FormData.cpp"
    "${_value_root}/HTTPHeaderMap.cpp" "${_value_root}/HTTPParsers.cpp"
    "${_value_root}/NetworkLoadMetrics.cpp" "${_value_root}/CacheValidation.cpp"
    "${WEBCORE_DIR}/page/SecurityOrigin.cpp" "${WEBCORE_DIR}/page/SecurityOriginData.cpp"
    "${WEBCORE_DIR}/platform/LegacySchemeRegistry.cpp" "${WEBCORE_DIR}/platform/ProcessIdentifier.cpp"
    "${WEBCORE_DIR}/fileapi/ThreadableBlobRegistry.cpp" "${WEBCORE_DIR}/fileapi/BlobURL.cpp"
    "${WebCore_DERIVED_SOURCES_DIR}/HTTPHeaderNames.cpp"
    "${CHOS_WEBCORE_RESOURCE_TYPES_DIR}/ResourceRequestCHOS.cpp"
    "${CHOS_WEBCORE_RESOURCE_TYPES_DIR}/ResourceResponseCHOS.cpp"
    "${CHOS_WEBCORE_RESOURCE_TYPES_DIR}/ResourceErrorCHOS.cpp"
    "${CHOS_WEBCORE_RESOURCE_TYPES_DIR}/CertificateInfoCHOS.cpp"
    "${CHOS_WEBCORE_RESOURCE_TYPES_DIR}/CertificatePersistenceCHOS.cpp"
    "${WEBCORE_DIR}/platform/PublicSuffixStore.cpp"
    "${CHOS_WEBCORE_PLATFORM_DIR}/PublicSuffixStoreCHOS.cpp")
target_include_directories(CHOSNetworkValues PRIVATE "$<TARGET_PROPERTY:WebCore,INCLUDE_DIRECTORIES>")
target_compile_definitions(CHOSNetworkValues PRIVATE "$<TARGET_PROPERTY:WebCore,COMPILE_DEFINITIONS>")
target_link_libraries(CHOSNetworkValues PRIVATE WTF chos_publicsuffix CHOS::CertificateParser ICU::uc ICU::i18n ICU::data)
target_link_options(CHOSNetworkValues PRIVATE "-Wl,--strip-debug")
add_dependencies(CHOSNetworkValues WebCore_CopyPrivateHeaders WebCoreGeneratedSources CHOSWebCoreNames)
