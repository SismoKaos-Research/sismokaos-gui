qt_add_executable(sismokaos-gui
  main.cpp
  mainwindow.cpp mainwindow.h
)
target_link_libraries(sismokaos-gui PRIVATE skg_core ${SKG_MODULE_TARGETS} skg_warnings)
target_compile_definitions(sismokaos-gui PRIVATE SKG_VERSION="${PROJECT_VERSION}")
set_target_properties(sismokaos-gui PROPERTIES WIN32_EXECUTABLE ON MACOSX_BUNDLE ON)
