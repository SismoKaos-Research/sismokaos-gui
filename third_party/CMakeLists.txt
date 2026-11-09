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
