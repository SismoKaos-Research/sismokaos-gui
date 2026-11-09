qt_add_executable(sismokaos-gui
  main.cpp
  mainwindow.cpp mainwindow.h
  updatecheck.cpp updatecheck.h
  app.qrc
)
target_link_libraries(sismokaos-gui PRIVATE skg_core ${SKG_MODULE_TARGETS} Qt6::Network skg_warnings)
target_compile_definitions(sismokaos-gui PRIVATE SKG_VERSION="${PROJECT_VERSION}")
set_target_properties(sismokaos-gui PROPERTIES
  WIN32_EXECUTABLE ON
  MACOSX_BUNDLE ON
  MACOSX_BUNDLE_BUNDLE_NAME sismokaos-gui
  MACOSX_BUNDLE_GUI_IDENTIFIER org.sismokaos.gui
  MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
  MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})
