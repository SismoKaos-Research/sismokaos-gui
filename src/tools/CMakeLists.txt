qt_add_library(skg_tools STATIC
  toolregistry.cpp toolregistry.h
)
target_include_directories(skg_tools PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(skg_tools PUBLIC Qt6::Core skg_jobs PRIVATE skg_warnings)
list(APPEND SKG_TRANSLATED_TARGETS skg_tools)
set(SKG_TRANSLATED_TARGETS ${SKG_TRANSLATED_TARGETS} PARENT_SCOPE)
