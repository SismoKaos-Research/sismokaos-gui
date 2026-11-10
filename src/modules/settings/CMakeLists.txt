qt_add_library(skg_mod_settings STATIC
  settingsmodule.cpp settingsmodule.h
)
target_link_libraries(skg_mod_settings PUBLIC skg_core skg_study PRIVATE skg_warnings)
list(APPEND SKG_MODULE_TARGETS skg_mod_settings)
list(APPEND SKG_TRANSLATED_TARGETS skg_mod_settings)
set(SKG_MODULE_TARGETS ${SKG_MODULE_TARGETS} PARENT_SCOPE)
set(SKG_TRANSLATED_TARGETS ${SKG_TRANSLATED_TARGETS} PARENT_SCOPE)
