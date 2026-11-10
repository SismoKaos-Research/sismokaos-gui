qt_add_library(skg_core STATIC
  module.h
  defaults.h
  moduleregistry.h
  i18n.cpp i18n.h
  theme.cpp theme.h
  ui.cpp ui.h
)
# The style sheet and the icons, at :/theme/.
file(GLOB SKG_THEME_ICONS CONFIGURE_DEPENDS resources/icons/*.svg)
qt_add_resources(skg_core "theme" PREFIX "/theme" BASE resources
  FILES resources/theme.qss ${SKG_THEME_ICONS})
target_include_directories(skg_core PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(skg_core PUBLIC Qt6::Core Qt6::Widgets Qt6::Svg skg_jobs skg_tools
                                PRIVATE skg_warnings)
list(APPEND SKG_TRANSLATED_TARGETS skg_core)
set(SKG_TRANSLATED_TARGETS ${SKG_TRANSLATED_TARGETS} PARENT_SCOPE)
