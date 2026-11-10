# The feature-extraction port: Sismokaos-featureExtract as redesigned by
# sismokaos-cli (Rust), with that tool's current chaos parameters and
# calibration. Library only; the GUI module in src/modules/features drives it.
qt_add_library(skg_fx STATIC
  config.cpp    config.h
  mseed.cpp     mseed.h
  dsp.cpp       dsp.h
  npy.cpp       npy.h
  features.cpp  features.h
  catalog.cpp   catalog.h   csv.h
  preprocess.cpp preprocess.h
  calibrate.cpp calibrate.h
  engine.cpp    engine.h
)
target_include_directories(skg_fx PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(skg_fx PUBLIC Qt6::Core Qt6::Network miniz skg_jobs skg_text
                             PRIVATE skg_warnings)
list(APPEND SKG_TRANSLATED_TARGETS skg_fx)
set(SKG_TRANSLATED_TARGETS ${SKG_TRANSLATED_TARGETS} PARENT_SCOPE)
