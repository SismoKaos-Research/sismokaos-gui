qt_add_library(skg_mod_tdvms STATIC
  tdvmsmodule.cpp   tdvmsmodule.h
  ledgermodel.cpp   ledgermodel.h
  plandialog.cpp    plandialog.h
  mailboxdialog.cpp mailboxdialog.h
  coveragepane.cpp  coveragepane.h
  fdsnpane.cpp      fdsnpane.h
  stationmap.cpp    stationmap.h
  casedownloads.cpp casedownloads.h
)
target_link_libraries(skg_mod_tdvms PUBLIC skg_core skg_tdvms skg_study skg_studyui PRIVATE skg_warnings)
list(APPEND SKG_MODULE_TARGETS skg_mod_tdvms)
list(APPEND SKG_TRANSLATED_TARGETS skg_mod_tdvms)
set(SKG_MODULE_TARGETS ${SKG_MODULE_TARGETS} PARENT_SCOPE)
set(SKG_TRANSLATED_TARGETS ${SKG_TRANSLATED_TARGETS} PARENT_SCOPE)
