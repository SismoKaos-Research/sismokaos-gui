qt_add_library(skg_mod_classification STATIC
  classificationmodule.cpp classificationmodule.h
  settingspane.cpp         settingspane.h
  experimentspane.cpp      experimentspane.h
)
target_link_libraries(skg_mod_classification PUBLIC skg_core skg_sfc skg_mod_analysis PRIVATE skg_warnings)
list(APPEND SKG_MODULE_TARGETS skg_mod_classification)
list(APPEND SKG_TRANSLATED_TARGETS skg_mod_classification)
set(SKG_MODULE_TARGETS ${SKG_MODULE_TARGETS} PARENT_SCOPE)
set(SKG_TRANSLATED_TARGETS ${SKG_TRANSLATED_TARGETS} PARENT_SCOPE)
