# The ayzek launcher's logic (no Widgets): installation lookup, command line,
# output parsing. ayzek itself is built and shipped by third_party/.
qt_add_library(skg_ayzek STATIC launcher.cpp launcher.h)
target_include_directories(skg_ayzek PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(skg_ayzek PUBLIC Qt6::Core PRIVATE skg_warnings)
list(APPEND SKG_TRANSLATED_TARGETS skg_ayzek)
set(SKG_TRANSLATED_TARGETS ${SKG_TRANSLATED_TARGETS} PARENT_SCOPE)
