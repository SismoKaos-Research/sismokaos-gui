# Text as catalogs bring it: decoding CSV bytes (UTF-8, else Windows-1254)
# and restoring the letters an event service replaced by "?" in place names,
# from a list of populated places (data/PROVENANCE.md). QtCore only.
qt_add_library(skg_text STATIC
  text.cpp text.h
  data/text.qrc
)
target_include_directories(skg_text PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(skg_text PUBLIC Qt6::Core PRIVATE skg_warnings)
