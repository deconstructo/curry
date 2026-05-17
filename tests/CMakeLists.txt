add_executable(curry_test test_core.c)
target_link_libraries(curry_test PRIVATE curry_core)

add_test(NAME core           COMMAND curry_test)
add_test(NAME scheme_r7rs    COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/r7rs_tests.scm)
add_test(NAME numeric_ext    COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/numeric_ext_tests.scm)
add_test(NAME actors         COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/actors_tests.scm)
add_test(NAME dynamic_wind   COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/dynamic_wind_tests.scm)
add_test(NAME syntax_rules   COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/syntax_rules_tests.scm)
add_test(NAME ode            COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/ode_tests.scm)
add_test(NAME pde_numerical COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/pde_numerical_tests.scm)
add_test(NAME d_operator    COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/d_operator_tests.scm)
add_test(NAME tuples        COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/tuple_tests.scm)
add_test(NAME partial       COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/partial_tests.scm)

# Module tests need the build-tree module directory on the search path
set(MOD_PATH "${CMAKE_BINARY_DIR}/mods")
set_tests_properties(numeric_ext actors ode pde_numerical d_operator tuples partial PROPERTIES
    ENVIRONMENT "CURRY_MODULE_PATH=${MOD_PATH}")

if(BUILD_MODULE_GIT AND TARGET curry_git)
    add_test(NAME git COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/test_git.scm)
    set_tests_properties(git PROPERTIES
        ENVIRONMENT "CURRY_MODULE_PATH=${MOD_PATH}")
endif()

if(TARGET curry_plplot)
    add_test(NAME plplot COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/test_plplot.scm)
    set_tests_properties(plplot PROPERTIES
        ENVIRONMENT "CURRY_MODULE_PATH=${MOD_PATH}")
endif()

if(BUILD_MODULE_REDIS AND TARGET curry_redis)
    add_test(NAME redis
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/test_redis.sh
                $<TARGET_FILE:curry>
                ${MOD_PATH})
endif()

if(BUILD_MODULE_MQTT AND TARGET curry_mqtt)
    add_test(NAME mqtt
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/test_mqtt.sh
                $<TARGET_FILE:curry>
                ${MOD_PATH})
endif()

if(BUILD_MODULE_MCP AND TARGET curry_mcp)
    add_test(NAME mcp
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/test_mcp.sh
                $<TARGET_FILE:curry>
                ${MOD_PATH})
endif()

if(BUILD_MODULE_PROFILING AND TARGET curry_profiling)
    add_test(NAME profiling COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/profiling_tests.scm)
    set_tests_properties(profiling PROPERTIES
        ENVIRONMENT "CURRY_MODULE_PATH=${MOD_PATH}")
endif()

if(BUILD_MODULE_RPI AND TARGET curry_rpi)
    add_test(NAME rpi COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/test_rpi.scm)
    set_tests_properties(rpi PROPERTIES
        ENVIRONMENT "CURRY_MODULE_PATH=${MOD_PATH}")
endif()
