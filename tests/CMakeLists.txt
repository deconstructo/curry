add_executable(curry_test test_core.c)
target_link_libraries(curry_test PRIVATE curry_core)

add_test(NAME core           COMMAND curry_test)
add_test(NAME json           COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/json_tests.scm)
add_test(NAME sqlite         COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/sqlite_tests.scm)
add_test(NAME scheme_r7rs    COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/r7rs_tests.scm)
add_test(NAME scheme_r6rs    COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/r6rs_tests.scm)
add_test(NAME cond_expand    COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/cond_expand_tests.scm)
add_test(NAME srfi_s279_inspect COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/srfi_s279_inspect_tests.scm)
add_test(NAME include_relative COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/include_relative_tests.scm)
add_test(NAME define_library_stack_guard COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/define_library_stack_guard_tests.scm)
set_tests_properties(include_relative PROPERTIES
    ENVIRONMENT "CURRY_MODULE_PATH=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/include_relative")

# Both of these run the fixture's own main.scm directly as curry's
# positional script argument (not imported as a module) -- deliberately
# no WORKING_DIRECTORY override, so ctest's default cwd (the build
# directory) differs from the fixture's own directory, exercising the
# same cwd-independence these fixtures exist to check.
add_test(NAME load_dir_context_script_relative
    COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/load_dir_context/script_relative_load/main.scm)
add_test(NAME load_dir_context_exception_safety
    COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/load_dir_context/exception_safety/main.scm)
add_test(NAME load_dir_context_thread_safety
    COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/load_dir_context/thread_safety/main.scm)
add_test(NAME srfi_1 COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/srfi_1_tests.scm)
add_test(NAME srfi_253 COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/srfi_253_tests.scm)
add_test(NAME srfi_s26_cut COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/srfi_s26_cut_tests.scm)
add_test(NAME srfi_s14_char_sets COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/srfi_s14_char_sets_tests.scm)
add_test(NAME numeric_ext    COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/numeric_ext_tests.scm)
add_test(NAME actors         COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/actors_tests.scm)
# Rare, non-deterministic hang observed once in CI (Ubuntu Debug, under
# ctest -j full-suite contention): the 2000-actor spawn/race regression
# test never returned, but did not reproduce in 9 follow-up attempts
# (isolated runs and full-suite loops alike). A hard timeout turns any
# future recurrence into a fast, diagnosable CTest TIMEOUT instead of a
# silent 15+ minute CI hang.
set_tests_properties(actors PROPERTIES TIMEOUT 120)
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
add_test(NAME babylonian_astronomy COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/babylonian_astronomy_tests.scm)
add_test(NAME sx_rules      COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/sx_rules_tests.scm)
add_test(NAME sx_algebra    COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/sx_algebra_tests.scm)
add_test(NAME sx_poly       COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/sx_poly_tests.scm)
add_test(NAME sx_risch      COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/sx_risch_tests.scm)
add_test(NAME logic        COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/logic_tests.scm)
add_test(NAME aviation_weather COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/aviation_weather_tests.scm)
add_test(NAME naips COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/naips_tests.scm)
add_test(NAME base64 COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/base64_tests.scm)
add_test(NAME random       COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/random_tests.scm)
add_test(NAME sets         COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/set_tests.scm)
add_test(NAME sets_module  COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/sets_module_tests.scm)
add_test(NAME oop          COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/oop_tests.scm)
add_test(NAME yaml         COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/yaml_tests.scm)
add_test(NAME toml         COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/toml_tests.scm)
add_test(NAME matchable    COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/matchable_tests.scm)
add_test(NAME csv          COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/csv_tests.scm)
add_test(NAME schematic    COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/schematic_tests.scm)
add_test(NAME graphviz     COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/graphviz_tests.scm)
add_test(NAME xml          COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/xml_tests.scm)
add_test(NAME rss          COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/rss_tests.scm)
add_test(NAME atom         COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/atom_tests.scm)
add_test(NAME dot_locking  COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/dot_locking_tests.scm)
add_test(NAME zeromq       COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/zeromq_tests.scm)
add_test(NAME tts          COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/tts_tests.scm)
if(BUILD_FFI)
  # (curry sql) unconditionally imports (curry mariadb)/(curry postgres),
  # both of which import (curry ffi) -- all three tests need BUILD_FFI=ON
  # to even import, regardless of whether a live MariaDB/PostgreSQL
  # server is reachable (mariadb_tests.scm/postgres_tests.scm test what's
  # possible without one -- see their own headers).
  add_test(NAME sql          COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/sql_tests.scm)
  add_test(NAME mariadb      COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/mariadb_tests.scm)
  add_test(NAME postgres     COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/postgres_tests.scm)
endif()
add_test(NAME hash_tables_srfi COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/hash_tables_srfi_tests.scm)
add_test(NAME timespec COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/timespec_tests.scm)
add_test(NAME srfi_receive_assume_optional COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/srfi_receive_assume_optional_tests.scm)
add_test(NAME srfi_comparators COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/srfi_comparators_tests.scm)
add_test(NAME srfi_hash_tables_125_126 COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/srfi_hash_tables_125_126_tests.scm)
add_test(NAME srfi_sorting_vectors COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/srfi_sorting_vectors_tests.scm)
add_test(NAME srfi_sets_bags COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/srfi_sets_bags_tests.scm)
add_test(NAME srfi_generators COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/srfi_generators_tests.scm)
add_test(NAME srfi_threads COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/srfi_threads_tests.scm)
add_test(NAME srfi_misc COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/srfi_misc_tests.scm)
add_test(NAME srfi_numbered_shims COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/srfi_numbered_shims_tests.scm)
add_test(NAME srfi_numbered_shim_hash_family COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/srfi_numbered_shim_hash_family_tests.scm)
add_test(NAME srfi_numbered_shim_comparator_hash COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/srfi_numbered_shim_comparator_hash_tests.scm)
add_test(NAME srfi_srfi_prefixed_shims COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/srfi_srfi_prefixed_shims_tests.scm)
add_test(NAME srfi_s263_prototype_objects COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/srfi_s263_prototype_objects_tests.scm)
add_test(NAME srfi_s111_s195_boxes COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/srfi_s111_s195_boxes_tests.scm)
add_test(NAME srfi_s252_property_testing COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/srfi_s252_property_testing_tests.scm)
add_test(NAME srfi_s209_enums COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/srfi_s209_enums_tests.scm)
add_test(NAME srfi_s210_multiple_values COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/srfi_s210_multiple_values_tests.scm)
add_test(NAME srfi_s54_cat COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/srfi_s54_cat_tests.scm)
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
set_tests_properties(numeric_ext actors ode pde_numerical d_operator tuples partial trig pde_symbolic sicm sexagesimal logic sets_module random oop yaml PROPERTIES
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
    # (curry ncurses) is the same pure-Scheme-+-FFI shape as hdf5 above:
    # requires BUILD_FFI=ON to compile, dlopens the system ncurses at
    # runtime. ncurses_tests.scm only checks import/constants — no
    # initscr()/newwin(), which need a real controlling terminal.
    add_test(NAME ncurses COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/ncurses_tests.scm)
endif()

if(BUILD_MODULE_PROFILING AND TARGET curry_profiling)
    add_test(NAME profiling COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/profiling_tests.scm)
    set_tests_properties(profiling PROPERTIES
        ENVIRONMENT "CURRY_MODULE_PATH=${MOD_PATH}")
endif()

if(BUILD_MODULE_POSIX AND TARGET curry_posix)
    add_test(NAME posix COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/posix_tests.scm)
    set_tests_properties(posix PROPERTIES
        ENVIRONMENT "CURRY_MODULE_PATH=${MOD_PATH}")
    # (srfi s19 time) imports (curry posix) for current-time.
    add_test(NAME time_srfi COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/time_srfi_tests.scm)
    set_tests_properties(time_srfi PROPERTIES
        ENVIRONMENT "CURRY_MODULE_PATH=${MOD_PATH}")
    # (curry okf) imports (curry posix) for directory walking and (srfi 19)
    # for staleness/log timestamps.
    add_test(NAME okf COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/okf_tests.scm)
    set_tests_properties(okf PROPERTIES
        ENVIRONMENT "CURRY_MODULE_PATH=${MOD_PATH}")

    # (curry airports) imports (curry posix) for create-directory/
    # rename-file/set-environment-variable! and (curry http) for the
    # cache-refresh download. airports_tests.scm redirects HOME to an
    # isolated fixture cache before importing the module, so it never
    # touches the network or the real user cache.
    if(BUILD_MODULE_HTTP AND TARGET curry_http)
        add_test(NAME airports COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/airports_tests.scm)
        set_tests_properties(airports PROPERTIES
            ENVIRONMENT "CURRY_MODULE_PATH=${MOD_PATH}")
    endif()
endif()

if(BUILD_MODULE_CODESETS AND TARGET curry_codesets)
    add_test(NAME codesets COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/codesets_tests.scm)
    set_tests_properties(codesets PROPERTIES
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

if(BUILD_MODULE_TYPEDVEC AND TARGET curry_typedvec AND BUILD_MODULE_F64VECTOR AND TARGET curry_f64vector)
    # (srfi 4) imports both (curry typedvec) and (curry f64vector).
    add_test(NAME typedvec COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/typedvec_tests.scm)
    set_tests_properties(typedvec PROPERTIES
        ENVIRONMENT "CURRY_MODULE_PATH=${MOD_PATH}")
    # (srfi 160) builds on (srfi 4) in pure Scheme -- same module dependency.
    add_test(NAME srfi_160 COMMAND curry ${CMAKE_CURRENT_SOURCE_DIR}/srfi_160_tests.scm)
    set_tests_properties(srfi_160 PROPERTIES
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
