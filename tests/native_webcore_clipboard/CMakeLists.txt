# Pull only the needed original WebCore archive members into the guest fixture.
add_library(CHOSClipboardCore STATIC EXCLUDE_FROM_ALL $<TARGET_OBJECTS:WebCoreDOMAndRendering>)
add_executable(CHOSClipboardValues EXCLUDE_FROM_ALL main.cpp)
target_include_directories(CHOSClipboardValues PRIVATE "$<TARGET_PROPERTY:WebCore,INCLUDE_DIRECTORIES>")
target_compile_definitions(CHOSClipboardValues PRIVATE "$<TARGET_PROPERTY:WebCore,COMPILE_DEFINITIONS>")
target_link_libraries(CHOSClipboardValues PRIVATE "$<TARGET_FILE:WebCore>" CHOSClipboardCore WTF PAL Cairo::Cairo ICU::uc ICU::i18n ICU::data)
target_link_options(CHOSClipboardValues PRIVATE "-Wl,--strip-debug" "-Wl,--error-limit=0")
add_dependencies(CHOSClipboardValues WebCore)
