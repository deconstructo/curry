add_executable(curry_test test_core.c)
target_link_libraries(curry_test PRIVATE curry_core)

add_test(NAME core        COMMAND curry_test)
add_test(NAME scheme_r7rs COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/r7rs_tests.scm)
add_test(NAME numeric_ext COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/numeric_ext_tests.scm)
add_test(NAME actors      COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/actors_tests.scm)

# Module tests need the build-tree module directory on the search path
set(MOD_PATH "${CMAKE_BINARY_DIR}/mods")
set_tests_properties(numeric_ext actors PROPERTIES
    ENVIRONMENT "CURRY_MODULE_PATH=${MOD_PATH}")
