# Studies: the same analyses on several earthquakes (cases), the recipes
# that are run on them, the fixed layout of the results and what the
# comparison reads back from them. Library only (no Widgets); the GUI pages
# are in src/modules/study.
qt_add_library(skg_study STATIC
  study.cpp   study.h
  store.cpp   store.h
  runs.cpp    runs.h
  compare.cpp compare.h
  waveindex.cpp waveindex.h
)
target_include_directories(skg_study PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(skg_study PUBLIC Qt6::Core Qt6::Gui skg_analysis skg_cbi skg_sfc skg_fx skg_tdvms
                                PRIVATE tomlplusplus miniz skg_warnings)
list(APPEND SKG_TRANSLATED_TARGETS skg_study)
set(SKG_TRANSLATED_TARGETS ${SKG_TRANSLATED_TARGETS} PARENT_SCOPE)
