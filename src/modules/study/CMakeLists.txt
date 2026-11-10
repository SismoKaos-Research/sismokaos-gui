qt_add_library(skg_mod_study STATIC
  studymodule.cpp   studymodule.h
  studyswitcher.cpp studyswitcher.h
  casespage.cpp     casespage.h
  casedialogs.cpp   casedialogs.h
  casemap.cpp       casemap.h
  coveragedialog.cpp coveragedialog.h
  waveforms.cpp     waveforms.h
  studysetup.cpp    studysetup.h
  runspage.cpp      runspage.h
  comparepage.cpp   comparepage.h
  runtext.cpp       runtext.h
  runlog.cpp        runlog.h
  studydisk.cpp     studydisk.h
  settinghelp.cpp   settinghelp.h
)
target_link_libraries(skg_mod_study PUBLIC skg_core skg_study skg_studyui PRIVATE skg_warnings)
list(APPEND SKG_MODULE_TARGETS skg_mod_study)
list(APPEND SKG_TRANSLATED_TARGETS skg_mod_study)
set(SKG_MODULE_TARGETS ${SKG_MODULE_TARGETS} PARENT_SCOPE)
set(SKG_TRANSLATED_TARGETS ${SKG_TRANSLATED_TARGETS} PARENT_SCOPE)
