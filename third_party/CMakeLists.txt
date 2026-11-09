# Vendored libraries, each with a PROVENANCE.md (XGBoost's is beside its
# submodule as XGBOOST-PROVENANCE.md).
add_subdirectory(miniz)

# libsvm 3.36: two files, BSD-3.
add_library(libsvm STATIC libsvm/svm.cpp libsvm/svm.h)
target_include_directories(libsvm PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/libsvm)
set_target_properties(libsvm PROPERTIES POSITION_INDEPENDENT_CODE ON)

# toml++ 3.4.0: header only, MIT.
add_library(tomlplusplus INTERFACE)
target_include_directories(tomlplusplus INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/tomlplusplus)

# XGBoost 3.4.2 as a static library, CPU only. Apple's compiler has no OpenMP.
if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/xgboost/CMakeLists.txt)
  message(FATAL_ERROR "third_party/xgboost is empty: run `git submodule update --init --recursive`")
endif()
set(BUILD_STATIC_LIB ON CACHE BOOL "" FORCE)
set(KEEP_BUILD_ARTIFACTS_IN_BINARY_DIR ON CACHE BOOL "" FORCE)
set(GOOGLE_TEST OFF CACHE BOOL "" FORCE)
set(USE_CUDA OFF CACHE BOOL "" FORCE)
set(JVM_BINDINGS OFF CACHE BOOL "" FORCE)
set(R_LIB OFF CACHE BOOL "" FORCE)
set(BUILD_C_DOC OFF CACHE BOOL "" FORCE)
if(APPLE)
  set(USE_OPENMP OFF CACHE BOOL "" FORCE)
else()
  set(USE_OPENMP ON CACHE BOOL "" FORCE)
endif()
if(MSVC)
  set(FORCE_SHARED_CRT ON CACHE BOOL "" FORCE) # Qt links the dynamic CRT
endif()
add_subdirectory(xgboost EXCLUDE_FROM_ALL)

# ayzek 1.0.0 (GPL-3.0-or-later, AYZEK-PROVENANCE.md): not ported and not
# linked; built from the submodule as its own executable and staged with its
# models, licence and citation in <build>/ayzek/, which the packages carry
# beside the application. ayzek needs C++23, beyond the CI compilers, so it is
# compiled with zig (a recent clang and libc++, one static binary per
# platform) through Meson; see packaging/ayzek/.
set(AYZEK_SRC ${CMAKE_CURRENT_SOURCE_DIR}/ayzek)
find_program(SKG_NINJA ninja)
find_program(SKG_ZIG zig)
find_package(Python3 QUIET COMPONENTS Interpreter)
find_program(SKG_MESON meson)
set(SKG_MESON_CMD "${SKG_MESON}")
if(NOT SKG_MESON AND Python3_Interpreter_FOUND)
  execute_process(COMMAND ${Python3_EXECUTABLE} -c "import mesonbuild"
                  RESULT_VARIABLE _meson_module OUTPUT_QUIET ERROR_QUIET)
  if(_meson_module EQUAL 0)
    set(SKG_MESON_CMD "${Python3_EXECUTABLE};-m;mesonbuild.mesonmain")
  endif()
endif()
set(_ayzek_default OFF)
if(SKG_MESON_CMD AND SKG_NINJA AND SKG_ZIG AND Python3_Interpreter_FOUND
   AND EXISTS ${AYZEK_SRC}/meson.build)
  set(_ayzek_default ON)
endif()
option(SKG_BUNDLE_AYZEK "Build ayzek (third_party/ayzek) with zig and ship it beside the application" ${_ayzek_default})
set(SKG_AYZEK_DIR "" CACHE INTERNAL "staged ayzek directory in the build tree, empty when not bundled")
if(SKG_BUNDLE_AYZEK)
  if(NOT (SKG_MESON_CMD AND SKG_NINJA AND SKG_ZIG AND Python3_Interpreter_FOUND))
    message(FATAL_ERROR "SKG_BUNDLE_AYZEK needs meson, ninja, zig and python3 (meson='${SKG_MESON_CMD}' ninja='${SKG_NINJA}' zig='${SKG_ZIG}')")
  endif()
  if(NOT EXISTS ${AYZEK_SRC}/meson.build)
    message(FATAL_ERROR "third_party/ayzek is empty: run `git submodule update --init --recursive`")
  endif()
  # zig targets for this platform. macOS builds one per architecture and
  # joins them with lipo, as the application itself is universal.
  set(_ayzek_exe ayzek)
  if(APPLE)
    set(_ayzek_system darwin)
    set(_archs ${CMAKE_OSX_ARCHITECTURES})
    if(NOT _archs)
      set(_archs ${CMAKE_SYSTEM_PROCESSOR})
    endif()
    set(_ayzek_targets)
    foreach(_a IN LISTS _archs)
      if(_a MATCHES "arm64|aarch64")
        list(APPEND _ayzek_targets aarch64-macos)
      else()
        list(APPEND _ayzek_targets x86_64-macos)
      endif()
    endforeach()
  elseif(WIN32)
    set(_ayzek_system windows)
    set(_ayzek_targets x86_64-windows-gnu)
    set(_ayzek_exe ayzek.exe)
  else()
    set(_ayzek_system linux)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
      set(_ayzek_targets aarch64-linux-musl)
    else()
      set(_ayzek_targets x86_64-linux-musl)
    endif()
  endif()
  file(GLOB_RECURSE AYZEK_SOURCES CONFIGURE_DEPENDS
       ${AYZEK_SRC}/src/* ${AYZEK_SRC}/app/* ${AYZEK_SRC}/meson.build)
  set(_ayzek_work ${CMAKE_BINARY_DIR}/ayzek-build)
  set(_ayzek_built)
  foreach(_t IN LISTS _ayzek_targets)
    string(REGEX MATCH "^[^-]+" AYZEK_CPU ${_t})
    set(AYZEK_TARGET ${_t})
    set(AYZEK_SYSTEM ${_ayzek_system})
    set(AYZEK_PY "'${Python3_EXECUTABLE}'")
    set(AYZEK_WRAP "'${CMAKE_SOURCE_DIR}/packaging/ayzek/zigwrap.py'")
    set(AYZEK_ZIG "'${SKG_ZIG}'")
    configure_file(${CMAKE_SOURCE_DIR}/packaging/ayzek/cross.ini.in ${_ayzek_work}/${_t}.ini @ONLY)
    set(_out ${_ayzek_work}/${_t}/app/${_ayzek_exe})
    add_custom_command(OUTPUT ${_out}
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/packaging/ayzek/build.py
              --meson "${SKG_MESON_CMD}" --ninja ${SKG_NINJA} --source ${AYZEK_SRC}
              --build ${_ayzek_work}/${_t} --cross ${_ayzek_work}/${_t}.ini --exe ${_ayzek_exe}
      DEPENDS ${AYZEK_SOURCES} ${_ayzek_work}/${_t}.ini
              ${CMAKE_SOURCE_DIR}/packaging/ayzek/build.py ${CMAKE_SOURCE_DIR}/packaging/ayzek/zigwrap.py
      COMMENT "Building ayzek for ${_t} with zig"
      VERBATIM)
    list(APPEND _ayzek_built ${_out})
  endforeach()
  set(SKG_AYZEK_DIR ${CMAKE_BINARY_DIR}/ayzek CACHE INTERNAL "staged ayzek directory in the build tree, empty when not bundled")
  set(_staged ${SKG_AYZEK_DIR}/${_ayzek_exe})
  list(LENGTH _ayzek_built _n)
  if(_n GREATER 1)
    # lipo drops the ad-hoc signature zig wrote; macOS on Apple silicon
    # refuses an unsigned binary, so sign the joined one again.
    set(_join COMMAND lipo -create ${_ayzek_built} -output ${_staged}
              COMMAND codesign --force -s - ${_staged})
  else()
    set(_join COMMAND ${CMAKE_COMMAND} -E copy ${_ayzek_built} ${_staged})
  endif()
  add_custom_command(OUTPUT ${_staged}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${SKG_AYZEK_DIR}
    ${_join}
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${AYZEK_SRC}/models ${SKG_AYZEK_DIR}/models
    COMMAND ${CMAKE_COMMAND} -E copy ${AYZEK_SRC}/LICENSE ${AYZEK_SRC}/CITATION.cff
            ${CMAKE_CURRENT_SOURCE_DIR}/AYZEK-PROVENANCE.md ${SKG_AYZEK_DIR}
    DEPENDS ${_ayzek_built} ${AYZEK_SRC}/LICENSE ${AYZEK_SRC}/CITATION.cff
            ${CMAKE_CURRENT_SOURCE_DIR}/AYZEK-PROVENANCE.md
    COMMENT "Staging ayzek in ${SKG_AYZEK_DIR}"
    VERBATIM)
  add_custom_target(ayzek_bundle ALL DEPENDS ${_staged})
  if(NOT APPLE AND NOT WIN32)
    install(DIRECTORY ${SKG_AYZEK_DIR}/ DESTINATION share/sismokaos-gui/ayzek USE_SOURCE_PERMISSIONS)
  endif()
  message(STATUS "ayzek: built with zig for ${_ayzek_targets}, staged in ${SKG_AYZEK_DIR}")
else()
  message(STATUS "ayzek: not bundled (SKG_BUNDLE_AYZEK=OFF; needs meson, ninja, zig, python3)")
endif()
