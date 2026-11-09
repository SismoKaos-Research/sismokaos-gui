qt_add_library(skg_mod_earlywarning STATIC
  earlywarningmodule.cpp earlywarningmodule.h
)
target_link_libraries(skg_mod_earlywarning PUBLIC skg_core skg_ayzek skg_tools PRIVATE skg_warnings)
list(APPEND SKG_MODULE_TARGETS skg_mod_earlywarning)
list(APPEND SKG_TRANSLATED_TARGETS skg_mod_earlywarning)
set(SKG_MODULE_TARGETS ${SKG_MODULE_TARGETS} PARENT_SCOPE)
set(SKG_TRANSLATED_TARGETS ${SKG_TRANSLATED_TARGETS} PARENT_SCOPE)
