qt_add_library(skg_mod_features STATIC
  featuresmodule.cpp featuresmodule.h
  configpane.cpp     configpane.h
  bandsmodel.cpp     bandsmodel.h
  resultspane.cpp    resultspane.h
  npymodel.cpp       npymodel.h
)
target_link_libraries(skg_mod_features PUBLIC skg_core skg_fx PRIVATE skg_warnings)
list(APPEND SKG_MODULE_TARGETS skg_mod_features)
list(APPEND SKG_TRANSLATED_TARGETS skg_mod_features)
set(SKG_MODULE_TARGETS ${SKG_MODULE_TARGETS} PARENT_SCOPE)
set(SKG_TRANSLATED_TARGETS ${SKG_TRANSLATED_TARGETS} PARENT_SCOPE)
