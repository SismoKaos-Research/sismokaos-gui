qt_add_library(skg_jobs STATIC
  progress.cpp  progress.h
  job.cpp       job.h
  jobmodel.cpp  jobmodel.h
  jobspanel.cpp jobspanel.h
)
target_include_directories(skg_jobs PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(skg_jobs PUBLIC Qt6::Core Qt6::Widgets PRIVATE skg_warnings)
list(APPEND SKG_TRANSLATED_TARGETS skg_jobs)
set(SKG_TRANSLATED_TARGETS ${SKG_TRANSLATED_TARGETS} PARENT_SCOPE)
