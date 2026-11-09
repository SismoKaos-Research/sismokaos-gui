find_package(Qt6 REQUIRED COMPONENTS Test)

function(skg_test name)
  qt_add_executable(${name} ${ARGN})
  target_link_libraries(${name} PRIVATE Qt6::Test skg_jobs skg_tools skg_tdvms skg_warnings)
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

skg_test(tst_progress tst_progress.cpp)
skg_test(tst_job tst_job.cpp)
skg_test(tst_toolregistry tst_toolregistry.cpp)
skg_test(tst_tdvms_core tst_tdvms_core.cpp)
