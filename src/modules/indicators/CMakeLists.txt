qt_add_library(skg_mod_indicators STATIC
  indicatorsmodule.cpp indicatorsmodule.h
  resultspane.cpp      resultspane.h
)
target_link_libraries(skg_mod_indicators PUBLIC skg_core skg_cbi skg_mod_analysis skg_studyui PRIVATE skg_warnings)
list(APPEND SKG_MODULE_TARGETS skg_mod_indicators)
list(APPEND SKG_TRANSLATED_TARGETS skg_mod_indicators)
set(SKG_MODULE_TARGETS ${SKG_MODULE_TARGETS} PARENT_SCOPE)
set(SKG_TRANSLATED_TARGETS ${SKG_TRANSLATED_TARGETS} PARENT_SCOPE)
