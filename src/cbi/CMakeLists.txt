# The catalog-based indicators port (cbi): sliding-window seismic indicators
# from an earthquake catalog, after SismoKaos-Research/indicators-forecast,
# the tested fork of SismoKaos-Catalog-Based-Indicators. Library only (QtGui
# for the figures, no Widgets); the GUI module in src/modules/indicators
# drives it. The forecaster of the fork is not ported.
qt_add_library(skg_cbi STATIC
  catalog.cpp     catalog.h
  windows.cpp     windows.h
  indicators.cpp  indicators.h
  magnitudes.cpp  magnitudes.h
  decluster.cpp   decluster.h
  zones.cpp       zones.h
  table.cpp       table.h
  plots.cpp       plots.h
  run.cpp         run.h
  data/cbi.qrc
)
target_include_directories(skg_cbi PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(skg_cbi PUBLIC Qt6::Core Qt6::Gui skg_fx skg_analysis skg_sfc PRIVATE skg_warnings)
# Where the fault data files are in the source tree: the last place
# cbi::dataDirs() looks, so a build tree and the tests work uninstalled.
target_compile_definitions(skg_cbi PRIVATE SKG_SOURCE_DATA_DIR="${PROJECT_SOURCE_DIR}/data/faults")
list(APPEND SKG_TRANSLATED_TARGETS skg_cbi)
set(SKG_TRANSLATED_TARGETS ${SKG_TRANSLATED_TARGETS} PARENT_SCOPE)
