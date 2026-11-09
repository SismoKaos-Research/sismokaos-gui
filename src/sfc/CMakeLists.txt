# The classification port: seismic-features-classification (sfc) with its
# settings files, day-wise split, model tuning, metrics and reports. Models
# come from XGBoost, libsvm and an MLP of our own. Library only; the GUI
# module in src/modules/classification drives it.
qt_add_library(skg_sfc STATIC
  settings.cpp  settings.h
  dataset.cpp   dataset.h
  split.cpp     split.h
  metrics.cpp   metrics.h
)
target_include_directories(skg_sfc PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(skg_sfc PUBLIC Qt6::Core Qt6::Gui skg_fx skg_analysis xgboost libsvm tomlplusplus
                              PRIVATE skg_warnings)
list(APPEND SKG_TRANSLATED_TARGETS skg_sfc)
set(SKG_TRANSLATED_TARGETS ${SKG_TRANSLATED_TARGETS} PARENT_SCOPE)
