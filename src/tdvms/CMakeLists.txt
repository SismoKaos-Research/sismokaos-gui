# The tdvms port: an AFAD/TDVMS download campaign that supervises itself,
# as a library with no GUI in it. Each file mirrors a module of the Python
# tool (tdvms-pull); the GUI module in src/modules/tdvms drives it.
qt_add_library(skg_tdvms STATIC
  ledger.cpp    ledger.h
  plan.cpp      plan.h
  campaign.cpp  campaign.h
  slots.cpp     slots.h
  disk.cpp      disk.h
  settings.cpp  settings.h
  history.cpp   history.h
  report.cpp    report.h
  mail.cpp      mail.h
  imap.cpp      imap.h
  http.cpp      http.h
  portal.cpp    portal.h
  fetch.cpp     fetch.h
  coverage.cpp  coverage.h
  supervisor.cpp supervisor.h
  fdsn.cpp      fdsn.h
  runner.cpp    runner.h
  catalog.cpp   catalog.h
  data/data.qrc
)
target_include_directories(skg_tdvms PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(skg_tdvms PUBLIC Qt6::Core Qt6::Network miniz skg_jobs
                                 PRIVATE skg_warnings)
list(APPEND SKG_TRANSLATED_TARGETS skg_tdvms)
set(SKG_TRANSLATED_TARGETS ${SKG_TRANSLATED_TARGETS} PARENT_SCOPE)
