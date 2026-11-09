# miniz, vendored (see PROVENANCE.md). C, not C++; built once as a static lib.
add_library(miniz STATIC miniz.c miniz.h)
target_include_directories(miniz PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(miniz PROPERTIES LINKER_LANGUAGE C)
target_compile_definitions(miniz PUBLIC MINIZ_NO_ZLIB_COMPATIBLE_NAMES)
