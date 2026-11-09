qt_add_library(skg_core STATIC
  module.h
  moduleregistry.h
  i18n.cpp i18n.h
)
target_include_directories(skg_core PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(skg_core PUBLIC Qt6::Core Qt6::Widgets skg_jobs skg_tools
                                PRIVATE skg_warnings)
list(APPEND SKG_TRANSLATED_TARGETS skg_core)
set(SKG_TRANSLATED_TARGETS ${SKG_TRANSLATED_TARGETS} PARENT_SCOPE)
