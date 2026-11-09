qt_add_library(skg_mod_analysis STATIC
  analysismodule.cpp analysismodule.h
  figureview.cpp     figureview.h
  catalogpane.cpp    catalogpane.h
  labelspane.cpp     labelspane.h
  figurespane.cpp    figurespane.h
  gridpane.cpp       gridpane.h
)
target_link_libraries(skg_mod_analysis PUBLIC skg_core skg_analysis skg_mod_features PRIVATE skg_warnings)
list(APPEND SKG_MODULE_TARGETS skg_mod_analysis)
list(APPEND SKG_TRANSLATED_TARGETS skg_mod_analysis)
set(SKG_MODULE_TARGETS ${SKG_MODULE_TARGETS} PARENT_SCOPE)
set(SKG_TRANSLATED_TARGETS ${SKG_TRANSLATED_TARGETS} PARENT_SCOPE)
