# Widgets the tool pages share with the open study (the "use the study's
# inputs" bar). A library of its own so the tool modules need not link the
# study pages.
qt_add_library(skg_studyui STATIC
  studyinputs.cpp studyinputs.h
)
target_include_directories(skg_studyui PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(skg_studyui PUBLIC skg_core skg_study PRIVATE skg_warnings)
list(APPEND SKG_TRANSLATED_TARGETS skg_studyui)
set(SKG_TRANSLATED_TARGETS ${SKG_TRANSLATED_TARGETS} PARENT_SCOPE)
