qt_add_executable(sismokaos-gui
  main.cpp
  mainwindow.cpp mainwindow.h
  updatecheck.cpp updatecheck.h
  app.qrc
)
target_link_libraries(sismokaos-gui PRIVATE skg_core ${SKG_MODULE_TARGETS} Qt6::Network skg_warnings)
target_compile_definitions(sismokaos-gui PRIVATE SKG_VERSION="${PROJECT_VERSION}${SKG_VERSION_SUFFIX}")
set_target_properties(sismokaos-gui PROPERTIES
  WIN32_EXECUTABLE ON
  MACOSX_BUNDLE ON
  MACOSX_BUNDLE_BUNDLE_NAME sismokaos-gui
  MACOSX_BUNDLE_GUI_IDENTIFIER org.sismokaos.gui
  MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
  MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})

if(APPLE)
  add_custom_command(TARGET sismokaos-gui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy ${PROJECT_SOURCE_DIR}/LICENSE
            $<TARGET_BUNDLE_CONTENT_DIR:sismokaos-gui>/Resources/LICENSE VERBATIM)
endif()

# The fault data files (see the top-level CMakeLists.txt): in
# Contents/Resources/data of the macOS bundle, in data\ beside the executable
# on Windows; Linux packages take them from `cmake --install`.
if(APPLE)
  add_custom_command(TARGET sismokaos-gui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/data/faults
            $<TARGET_BUNDLE_CONTENT_DIR:sismokaos-gui>/Resources/data VERBATIM)
elseif(WIN32)
  add_custom_command(TARGET sismokaos-gui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/data/faults
            $<TARGET_FILE_DIR:sismokaos-gui>/data VERBATIM)
endif()

# ayzek, when bundled (third_party/CMakeLists.txt): beside the executable on
# Windows, in Contents/Resources in the macOS bundle; Linux packages take it
# from `cmake --install` (share/sismokaos-gui/ayzek).
if(SKG_AYZEK_DIR)
  add_dependencies(sismokaos-gui ayzek_bundle)
  if(APPLE)
    add_custom_command(TARGET sismokaos-gui POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory ${SKG_AYZEK_DIR}
              $<TARGET_BUNDLE_CONTENT_DIR:sismokaos-gui>/Resources/ayzek VERBATIM)
  elseif(WIN32)
    add_custom_command(TARGET sismokaos-gui POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory ${SKG_AYZEK_DIR}
              $<TARGET_FILE_DIR:sismokaos-gui>/ayzek VERBATIM)
  endif()
endif()
