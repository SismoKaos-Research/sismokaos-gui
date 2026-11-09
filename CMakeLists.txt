cmake_minimum_required(VERSION 3.21)
project(sismokaos-gui VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets LinguistTools)
qt_standard_project_setup(I18N_TRANSLATED_LANGUAGES tr en)

add_library(skg_warnings INTERFACE)
target_compile_options(skg_warnings INTERFACE
  $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
  $<$<CXX_COMPILER_ID:MSVC>:/W4>)

# Layered libraries, each in its own directory with its own CMakeLists.txt:
#   jobs    running external tools, progress, the jobs table and panel
#   tools   where the sismokaos/sfc/tdvms executables are and how to call them
#   core    the Module interface, AppContext, registry, language switching
#   modules/<name>  one GUI module (a tab) per directory
#   app     the main window and main()
add_subdirectory(src/jobs)
add_subdirectory(src/tools)
add_subdirectory(src/core)
add_subdirectory(src/modules)
add_subdirectory(src/app)

# Translations: `cmake --build build --target update_translations` refreshes
# the .ts files from every target below; the .qm files are embedded at :/i18n/.
qt_add_translations(TARGETS sismokaos-gui ${SKG_TRANSLATED_TARGETS}
  TS_FILE_BASE sismokaos-gui
  TS_FILE_DIR translations
  RESOURCE_PREFIX "/i18n")

install(TARGETS sismokaos-gui RUNTIME DESTINATION bin BUNDLE DESTINATION .)

enable_testing()
add_subdirectory(tests)
