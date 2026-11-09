qt_add_library(skg_mod_placeholder STATIC
  placeholdermodule.cpp placeholdermodule.h
)
target_link_libraries(skg_mod_placeholder PUBLIC skg_core PRIVATE skg_warnings)
list(APPEND SKG_MODULE_TARGETS skg_mod_placeholder)
list(APPEND SKG_TRANSLATED_TARGETS skg_mod_placeholder)
set(SKG_MODULE_TARGETS ${SKG_MODULE_TARGETS} PARENT_SCOPE)
set(SKG_TRANSLATED_TARGETS ${SKG_TRANSLATED_TARGETS} PARENT_SCOPE)
