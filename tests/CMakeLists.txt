add_executable(curry_test test_core.c)
target_link_libraries(curry_test PRIVATE curry_core)

add_test(NAME core           COMMAND curry_test)
add_test(NAME json           COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/json_tests.scm)
add_test(NAME scheme_r7rs    COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/r7rs_tests.scm)
add_test(NAME scheme_r6rs    COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/r6rs_tests.scm)
add_test(NAME numeric_ext    COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/numeric_ext_tests.scm)
add_test(NAME actors         COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/actors_tests.scm)
add_test(NAME dynamic_wind   COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/dynamic_wind_tests.scm)
add_test(NAME syntax_rules   COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/syntax_rules_tests.scm)
add_test(NAME ode            COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/ode_tests.scm)
add_test(NAME pde_numerical COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/pde_numerical_tests.scm)
add_test(NAME d_operator    COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/d_operator_tests.scm)
add_test(NAME tuples        COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/tuple_tests.scm)
add_test(NAME partial       COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/partial_tests.scm)
add_test(NAME trig              COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/trig_tests.scm)
add_test(NAME transcendental   COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/transcendental_tests.scm)
add_test(NAME pde_symbolic     COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/pde_symbolic_tests.scm)
add_test(NAME sicm              COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/sicm_tests.scm)
add_test(NAME akkadian      COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/akkadian_tests.scm)
add_test(NAME conditions    COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/conditions_tests.scm)
add_test(NAME numtheory     COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/numtheory_tests.scm)
add_test(NAME sexagesimal   COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/sexagesimal_tests.scm)
add_test(NAME sx_rules      COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/sx_rules_tests.scm)
add_test(NAME sx_algebra    COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/sx_algebra_tests.scm)
add_test(NAME sx_poly       COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/sx_poly_tests.scm)
add_test(NAME sx_risch      COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/sx_risch_tests.scm)
add_test(NAME logic        COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/logic_tests.scm)
add_test(NAME random       COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/random_tests.scm)
add_test(NAME sets         COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/set_tests.scm)
add_test(NAME sets_module  COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/sets_module_tests.scm)
if(BUILD_MPFR)
  add_test(NAME mpfr COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/mpfr_tests.scm)
endif()
add_test(NAME cli
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/test_cli.sh
            $<TARGET_FILE:curry>)
add_test(NAME debugger
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/test_debugger.sh
            $<TARGET_FILE:curry>)

# Module tests need the build-tree module directory on the search path
set(MOD_PATH "${CMAKE_BINARY_DIR}/mods")
set_tests_properties(akkadian PROPERTIES
    ENVIRONMENT "CURRY_MODULE_PATH=${MOD_PATH}")
set_tests_properties(numeric_ext actors ode pde_numerical d_operator tuples partial trig pde_symbolic sicm sexagesimal logic sets_module random PROPERTIES
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

if(BUILD_MODULE_LSP AND TARGET curry_lsp)
    add_test(NAME lsp
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/test_lsp.sh
                $<TARGET_FILE:curry>
                ${MOD_PATH})
endif()

if(BUILD_LLVM)
    add_test(NAME jit COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/jit_tests.scm)
endif()

if(BUILD_MODULE_NETWORK AND TARGET curry_network)
    add_test(NAME network COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/network_tests.scm)
    set_tests_properties(network PROPERTIES ENVIRONMENT "CURRY_MODULE_PATH=${MOD_PATH}")
endif()

# Pure-Scheme scientific I/O modules (fits/netcdf) plus the FFI-based hdf5
# wrapper (requires BUILD_FFI=ON to compile; libhdf5 itself is a
# runtime-only dlopen dependency, so hdf5_tests.scm skips cleanly if it's
# not installed rather than needing a build-time gate here).
add_test(NAME fits    COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/fits_tests.scm)
add_test(NAME netcdf  COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/netcdf_tests.scm)
if(BUILD_FFI)
    add_test(NAME hdf5 COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/hdf5_tests.scm)
endif()

if(BUILD_MODULE_PROFILING AND TARGET curry_profiling)
    add_test(NAME profiling COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/profiling_tests.scm)
    set_tests_properties(profiling PROPERTIES
        ENVIRONMENT "CURRY_MODULE_PATH=${MOD_PATH}")
endif()

if(BUILD_MODULE_RPI AND TARGET curry_rpi)
    # rpi_tests.scm: hardware-free simulation tests + API surface check (Linux)
    add_test(NAME rpi COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/rpi_tests.scm)
    set_tests_properties(rpi PROPERTIES
        ENVIRONMENT "CURRY_MODULE_PATH=${MOD_PATH}")
    # test_rpi.scm: smoke tests that open real device nodes (skipped if absent)
    add_test(NAME rpi_hw COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/test_rpi.scm)
    set_tests_properties(rpi_hw PROPERTIES
        ENVIRONMENT "CURRY_MODULE_PATH=${MOD_PATH}")
endif()

if(BUILD_MODULE_QT6 AND TARGET curry_qt6)
    # Phase 1 (widget creation) runs immediately; Phase 2 (painter / text
    # metrics) triggers an offscreen render via canvas-save-png!.
    # QT_QPA_PLATFORM=offscreen lets this run in CI without a display.
    add_test(NAME qt6 COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/test_qt6_new.scm)
    set_tests_properties(qt6 PROPERTIES
        ENVIRONMENT "CURRY_MODULE_PATH=${MOD_PATH};QT_QPA_PLATFORM=offscreen"
        TIMEOUT 30)
endif()
