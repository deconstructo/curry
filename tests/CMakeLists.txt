add_executable(curry_test test_core.c)
target_link_libraries(curry_test PRIVATE curry_core)

add_test(NAME core COMMAND curry_test)
add_test(NAME scheme_r7rs COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/r7rs_tests.scm)
