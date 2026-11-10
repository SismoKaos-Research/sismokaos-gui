qt_add_library(skg_core STATIC
  module.h
  defaults.h
  moduleregistry.h
  i18n.cpp i18n.h
  theme.cpp theme.h
  ui.cpp ui.h
  keepawake.cpp keepawake.h
  appsettings.h
)
# The style sheet and the icons, at :/theme/.
file(GLOB SKG_THEME_ICONS CONFIGURE_DEPENDS resources/icons/*.svg)
qt_add_resources(skg_core "theme" PREFIX "/theme" BASE resources
  FILES resources/theme.qss ${SKG_THEME_ICONS})
# Qt's own translations (the standard buttons, the file dialog) for the
# languages of the application, embedded at :/qt-i18n/ so that every package
# has them without a deployment step: macdeployqt copies none, windeployqt
# renames them. A Qt without the file gives a build whose standard buttons
# stay English, and says so here.
file(GLOB SKG_APP_TS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/translations/sismokaos-gui_*.ts)
# Where this Qt keeps them: asked of Qt itself (qtpaths, else qmake), which
# knows better than the prefix CMake found the packages under.
set(SKG_QT_TRANSLATIONS "")
foreach(tool Qt6::qtpaths Qt6::qmake)
  if(NOT SKG_QT_TRANSLATIONS AND TARGET ${tool})
    get_target_property(tool_path ${tool} IMPORTED_LOCATION)
    if(tool_path)
      execute_process(COMMAND "${tool_path}" -query QT_INSTALL_TRANSLATIONS
        OUTPUT_VARIABLE SKG_QT_TRANSLATIONS OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    endif()
  endif()
endforeach()
if(NOT SKG_QT_TRANSLATIONS OR NOT IS_DIRECTORY "${SKG_QT_TRANSLATIONS}")
  set(SKG_QT_TRANSLATIONS "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_TRANSLATIONS}")
endif()
file(TO_CMAKE_PATH "${SKG_QT_TRANSLATIONS}" SKG_QT_TRANSLATIONS)
set(SKG_QT_QM "")
foreach(ts IN LISTS SKG_APP_TS)
  get_filename_component(ts_name ${ts} NAME_WE)            # sismokaos-gui_tr
  string(REGEX REPLACE "^.*_" "" ts_lang "${ts_name}")     # tr
  if(ts_lang STREQUAL "en")
    continue()
  endif()
  set(qm "${SKG_QT_TRANSLATIONS}/qtbase_${ts_lang}.qm")
  if(EXISTS "${qm}")
    file(COPY "${qm}" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/qt-i18n")
    list(APPEND SKG_QT_QM "${CMAKE_CURRENT_BINARY_DIR}/qt-i18n/qtbase_${ts_lang}.qm")
    message(STATUS "Qt's own '${ts_lang}' translation: embedded from ${qm}")
  else()
    message(WARNING "Qt's own '${ts_lang}' translation was not found at ${qm}: "
                    "the standard buttons stay English unless the Qt at run time has it")
  endif()
endforeach()
if(SKG_QT_QM)
  qt_add_resources(skg_core "qt_i18n" PREFIX "/qt-i18n"
    BASE "${CMAKE_CURRENT_BINARY_DIR}/qt-i18n" FILES ${SKG_QT_QM})
endif()
target_include_directories(skg_core PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(skg_core PUBLIC Qt6::Core Qt6::Widgets Qt6::Svg skg_jobs skg_tools
                                PRIVATE skg_warnings)
list(APPEND SKG_TRANSLATED_TARGETS skg_core)
set(SKG_TRANSLATED_TARGETS ${SKG_TRANSLATED_TARGETS} PARENT_SCOPE)
