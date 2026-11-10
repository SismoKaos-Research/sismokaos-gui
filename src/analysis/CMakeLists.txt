# The analysis port: SCAP's catalog and fault-distance questions, earthquake
# labelling of feature tables, and the figures. Library only (QtGui for the
# rendering, no Widgets); the GUI module in src/modules/analysis drives it.
qt_add_library(skg_analysis STATIC
  geo.cpp        geo.h
  events.cpp     events.h
  shapefile.cpp  shapefile.h
  faults.cpp     faults.h
  label.cpp      label.h
  series.cpp     series.h
  plot.cpp       plot.h
  map.cpp        map.h
  grid.cpp       grid.h
  regions.cpp    regions.h
)
target_include_directories(skg_analysis PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(skg_analysis PUBLIC Qt6::Core Qt6::Gui Qt6::Svg skg_fx PRIVATE skg_warnings)
list(APPEND SKG_TRANSLATED_TARGETS skg_analysis)
set(SKG_TRANSLATED_TARGETS ${SKG_TRANSLATED_TARGETS} PARENT_SCOPE)
