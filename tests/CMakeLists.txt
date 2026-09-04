add_executable(curry_test test_core.c)
target_link_libraries(curry_test PRIVATE curry_core)

add_test(NAME core           COMMAND curry_test)
add_test(NAME json           COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/json_tests.scm)
add_test(NAME sqlite         COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/sqlite_tests.scm)
add_test(NAME scheme_r7rs    COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/r7rs_tests.scm)
add_test(NAME scheme_r6rs    COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/r6rs_tests.scm)
add_test(NAME cond_expand    COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/cond_expand_tests.scm)
add_test(NAME srfi_s279_inspect COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/srfi_s279_inspect_tests.scm)
add_test(NAME include_relative COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/include_relative_tests.scm)
add_test(NAME define_library_stack_guard COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/define_library_stack_guard_tests.scm)
add_test(NAME module_isolation COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/module_isolation_tests.scm)
set_tests_properties(include_relative PROPERTIES
    ENVIRONMENT "CURRY_MODULE_PATH=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/include_relative")

# Both of these run the fixture's own main.scm directly as curry's
# positional script argument (not imported as a module) -- deliberately
# no WORKING_DIRECTORY override, so ctest's default cwd (the build
# directory) differs from the fixture's own directory, exercising the
# same cwd-independence these fixtures exist to check.
add_test(NAME load_dir_context_script_relative
    COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/load_dir_context/script_relative_load/main.scm)
add_test(NAME load_dir_context_exception_safety
    COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/load_dir_context/exception_safety/main.scm)
add_test(NAME load_dir_context_thread_safety
    COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/load_dir_context/thread_safety/main.scm)
add_test(NAME srfi_1 COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/srfi_1_tests.scm)
add_test(NAME srfi_215 COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/srfi_215_tests.scm)
add_test(NAME srfi_253 COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/srfi_253_tests.scm)
add_test(NAME srfi_s26_cut COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/srfi_s26_cut_tests.scm)
add_test(NAME srfi_s14_char_sets COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/srfi_s14_char_sets_tests.scm)
add_test(NAME numeric_ext    COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/numeric_ext_tests.scm)
add_test(NAME actors         COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/actors_tests.scm)
# Rare, non-deterministic hang observed once in CI (Ubuntu Debug, under
# ctest -j full-suite contention): the 2000-actor spawn/race regression
# test never returned, but did not reproduce in 9 follow-up attempts
# (isolated runs and full-suite loops alike). A hard timeout turns any
# future recurrence into a fast, diagnosable CTest TIMEOUT instead of a
# silent 15+ minute CI hang.
set_tests_properties(actors PROPERTIES TIMEOUT 120)
add_test(NAME dynamic_wind   COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/dynamic_wind_tests.scm)
add_test(NAME disassemble    COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/disassemble_tests.scm)
add_test(NAME inliner        COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/inliner_tests.scm)
add_test(NAME apply_tco      COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/apply_tco_tests.scm)
add_test(NAME syntax_rules   COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/syntax_rules_tests.scm)
add_test(NAME syntax_rules_nested_ellipsis COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/syntax_rules_nested_ellipsis_tests.scm)
add_test(NAME ode            COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/ode_tests.scm)
add_test(NAME pde_numerical COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/pde_numerical_tests.scm)
add_test(NAME d_operator    COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/d_operator_tests.scm)
add_test(NAME tuples        COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/tuple_tests.scm)
add_test(NAME partial       COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/partial_tests.scm)
add_test(NAME trig              COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/trig_tests.scm)
add_test(NAME transcendental   COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/transcendental_tests.scm)
add_test(NAME pde_symbolic     COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/pde_symbolic_tests.scm)
add_test(NAME sicm              COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/sicm_tests.scm)
add_test(NAME akkadian      COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/akkadian_tests.scm)
add_test(NAME reader_dotted_list COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/reader_dotted_list_tests.scm)
add_test(NAME conditions    COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/conditions_tests.scm)
add_test(NAME numtheory     COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/numtheory_tests.scm)
add_test(NAME sexagesimal   COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/sexagesimal_tests.scm)
add_test(NAME babylonian_astronomy COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/babylonian_astronomy_tests.scm)
add_test(NAME sx_rules      COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/sx_rules_tests.scm)
add_test(NAME sx_algebra    COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/sx_algebra_tests.scm)
add_test(NAME sx_poly       COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/sx_poly_tests.scm)
add_test(NAME sx_risch      COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/sx_risch_tests.scm)
add_test(NAME logic        COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/logic_tests.scm)
add_test(NAME aviation_weather COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/aviation_weather_tests.scm)
add_test(NAME naips COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/naips_tests.scm)
add_test(NAME base64 COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/base64_tests.scm)
add_test(NAME random       COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/random_tests.scm)
add_test(NAME gillespie    COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/gillespie_tests.scm)
add_test(NAME sets         COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/set_tests.scm)
add_test(NAME sets_module  COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/sets_module_tests.scm)
add_test(NAME oop          COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/oop_tests.scm)
add_test(NAME yaml         COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/yaml_tests.scm)
add_test(NAME toml         COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/toml_tests.scm)
add_test(NAME matchable    COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/matchable_tests.scm)
add_test(NAME csv          COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/csv_tests.scm)
add_test(NAME schematic    COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/schematic_tests.scm)
add_test(NAME graphviz     COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/graphviz_tests.scm)
add_test(NAME xml          COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/xml_tests.scm)
add_test(NAME rss          COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/rss_tests.scm)
add_test(NAME atom         COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/atom_tests.scm)
add_test(NAME dot_locking  COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/dot_locking_tests.scm)
add_test(NAME zeromq       COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/zeromq_tests.scm)
add_test(NAME tts          COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/tts_tests.scm)
if(BUILD_FFI)
  # (curry sql) unconditionally imports (curry mariadb)/(curry postgres),
  # both of which import (curry ffi) -- all three tests need BUILD_FFI=ON
  # to even import, regardless of whether a live MariaDB/PostgreSQL
  # server is reachable (mariadb_tests.scm/postgres_tests.scm test what's
  # possible without one -- see their own headers).
  add_test(NAME sql          COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/sql_tests.scm)
  add_test(NAME mariadb      COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/mariadb_tests.scm)
  add_test(NAME postgres     COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/postgres_tests.scm)
endif()
add_test(NAME hash_tables_srfi COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/hash_tables_srfi_tests.scm)
add_test(NAME timespec COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/timespec_tests.scm)
add_test(NAME srfi_receive_assume_optional COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/srfi_receive_assume_optional_tests.scm)
add_test(NAME srfi_comparators COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/srfi_comparators_tests.scm)
add_test(NAME srfi_hash_tables_125_126 COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/srfi_hash_tables_125_126_tests.scm)
add_test(NAME srfi_sorting_vectors COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/srfi_sorting_vectors_tests.scm)
add_test(NAME srfi_sets_bags COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/srfi_sets_bags_tests.scm)
add_test(NAME srfi_generators COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/srfi_generators_tests.scm)
add_test(NAME srfi_threads COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/srfi_threads_tests.scm)
add_test(NAME srfi_misc COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/srfi_misc_tests.scm)
add_test(NAME srfi_numbered_shims COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/srfi_numbered_shims_tests.scm)
add_test(NAME srfi_numbered_shim_hash_family COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/srfi_numbered_shim_hash_family_tests.scm)
add_test(NAME srfi_numbered_shim_comparator_hash COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/srfi_numbered_shim_comparator_hash_tests.scm)
add_test(NAME srfi_srfi_prefixed_shims COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/srfi_srfi_prefixed_shims_tests.scm)
add_test(NAME srfi_s263_prototype_objects COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/srfi_s263_prototype_objects_tests.scm)
add_test(NAME srfi_s111_s195_boxes COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/srfi_s111_s195_boxes_tests.scm)
add_test(NAME srfi_s252_property_testing COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/srfi_s252_property_testing_tests.scm)
add_test(NAME srfi_s209_enums COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/srfi_s209_enums_tests.scm)
add_test(NAME srfi_s210_multiple_values COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/srfi_s210_multiple_values_tests.scm)
add_test(NAME srfi_s54_cat COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/srfi_s54_cat_tests.scm)
add_test(NAME srfi_9_31_45 COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/srfi_9_31_45_tests.scm)
add_test(NAME case_lambda COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/case_lambda_tests.scm)
add_test(NAME websocket COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/websocket_tests.scm)
add_test(NAME websocket_server COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/websocket_server_tests.scm)
add_test(NAME ros COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/ros_tests.scm)
# Issue #110: observed in CI (macos-latest, Release, under ctest -j
# full-suite contention, twice in a row on 2026-08-31) -- the "websocket"
# test hung completely silent for 28+ minutes until the whole job was
# cancelled, while "ros" (the same run) failed fast and cleanly with
# "tcp-listen: bind failed on port 17995". Root cause: these three tests
# each bound fixed, hardcoded port numbers, inherently vulnerable to
# collision under parallel CI load against another process (a lingering
# listener from a previous run, a system service, another test) already
# bound to the same port. Fixed at the root (not just papered over): all
# three now bind port 0 (an OS-assigned ephemeral port) and read back the
# actual port via socket-local-port/ws-listener-port
# (modules/network/srfi106.c, lib/curry/modules/curry/websocket.scm) --
# verified by holding all six of the OLD hardcoded ports busy from an
# external process and confirming all three suites still pass cleanly,
# since none of them depend on any fixed port anymore.
#
# The TIMEOUT below is kept anyway as defense-in-depth, matching the
# "actors" test's own identical treatment just above for its unrelated
# rare hang: even with the port-contention root cause closed, an
# open-ended hang from some OTHER cause is still better caught as a fast,
# diagnosable CTest TIMEOUT than a silent 28+ minute CI stall.
set_tests_properties(websocket websocket_server ros PROPERTIES TIMEOUT 60)
add_test(NAME srfi_95_78_212 COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/srfi_95_78_212_tests.scm)
add_test(NAME srfi_141 COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/srfi_141_tests.scm)
add_test(NAME srfi_41 COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/srfi_41_tests.scm)
if(BUILD_MPFR)
  add_test(NAME mpfr COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/mpfr_tests.scm)
endif()
add_test(NAME cli
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/test_cli.sh
            $<TARGET_FILE:curry>)
# Issue #152's ,env regression subtest races two actors against a busy-wait
# poll loop (env152-loop) whose termination depends on both actors actually
# finishing -- the same open-ended-hang risk shape actors/websocket/ros
# already carry an explicit TIMEOUT for below, and for the same reason: a
# real hang here (e.g. a livelock in env.c's seqlock retry loop under
# sustained writer contention, see issue #153) would otherwise block only
# up to CI's 30-minute job-level timeout with no isolated, diagnosable
# ctest-level signal. cli normally completes in under 70s (observed up to
# ~70s in CI's own websocket-retry runs, ~25s locally); 300s gives
# generous headroom above that while still turning an open-ended hang into
# a fast, clearly-labeled TIMEOUT instead.
set_tests_properties(cli PROPERTIES TIMEOUT 300)
add_test(NAME debugger
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/test_debugger.sh
            $<TARGET_FILE:curry>)

# Module tests need the build-tree module directory on the search path
set(MOD_PATH "${CMAKE_BINARY_DIR}/mods")
set_tests_properties(akkadian PROPERTIES
    ENVIRONMENT "CURRY_MODULE_PATH=${MOD_PATH}")
set_tests_properties(numeric_ext actors ode pde_numerical d_operator tuples partial trig pde_symbolic sicm sexagesimal logic sets_module random gillespie oop yaml PROPERTIES
    ENVIRONMENT "CURRY_MODULE_PATH=${MOD_PATH}")

if(BUILD_MODULE_GIT AND TARGET curry_git)
    add_test(NAME git COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/test_git.scm)
    set_tests_properties(git PROPERTIES
        ENVIRONMENT "CURRY_MODULE_PATH=${MOD_PATH}")
endif()

if(TARGET curry_plplot)
    add_test(NAME plplot COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/test_plplot.scm)
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
    add_test(NAME jit COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/jit_tests.scm)
endif()

if(BUILD_MODULE_NETWORK AND TARGET curry_network)
    add_test(NAME network COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/network_tests.scm)
    set_tests_properties(network PROPERTIES ENVIRONMENT "CURRY_MODULE_PATH=${MOD_PATH}")
    add_test(NAME srfi_106 COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/srfi_106_tests.scm)
    set_tests_properties(srfi_106 PROPERTIES ENVIRONMENT "CURRY_MODULE_PATH=${MOD_PATH}")
endif()

# libpiper has no system package yet (see docs/reference/module-piper.md)
# and is OFF by default -- gated the same way BUILD_FFI-dependent suites
# below are, so this suite simply doesn't exist in ctest's list at all on
# an ordinary build, rather than being present and unconditionally
# failing to import.
if(BUILD_MODULE_PIPER AND TARGET curry_piper)
    add_test(NAME piper COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/piper_tests.scm)
    set_tests_properties(piper PROPERTIES ENVIRONMENT "CURRY_MODULE_PATH=${MOD_PATH}")
endif()

# (curry s3) imports (curry crypto) for HMAC-SHA256/SHA-256 and
# (curry http) for the actual requests -- both needed to even import,
# regardless of whether a live S3-compatible endpoint is reachable
# (s3_tests.scm's own live round-trip section skips cleanly unless
# CURRY_S3_TEST_ENDPOINT/BUCKET/ACCESS_KEY/SECRET_KEY are all set).
if(BUILD_MODULE_CRYPTO AND TARGET curry_crypto AND BUILD_MODULE_HTTP AND TARGET curry_http)
    add_test(NAME s3 COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/s3_tests.scm)
    set_tests_properties(s3 PROPERTIES ENVIRONMENT "CURRY_MODULE_PATH=${MOD_PATH}")
endif()

# Pure-Scheme scientific I/O modules (fits/netcdf) plus the FFI-based hdf5
# wrapper (requires BUILD_FFI=ON to compile; libhdf5 itself is a
# runtime-only dlopen dependency, so hdf5_tests.scm skips cleanly if it's
# not installed rather than needing a build-time gate here).
add_test(NAME fits    COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/fits_tests.scm)
add_test(NAME netcdf  COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/netcdf_tests.scm)
if(BUILD_FFI)
    add_test(NAME hdf5 COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/hdf5_tests.scm)
    # (curry ncurses) is the same pure-Scheme-+-FFI shape as hdf5 above:
    # requires BUILD_FFI=ON to compile, dlopens the system ncurses at
    # runtime. ncurses_tests.scm only checks import/constants — no
    # initscr()/newwin(), which need a real controlling terminal.
    add_test(NAME ncurses COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/ncurses_tests.scm)
endif()

if(BUILD_MODULE_PROFILING AND TARGET curry_profiling)
    add_test(NAME profiling COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/profiling_tests.scm)
    set_tests_properties(profiling PROPERTIES
        ENVIRONMENT "CURRY_MODULE_PATH=${MOD_PATH}")
endif()

if(BUILD_MODULE_POSIX AND TARGET curry_posix)
    add_test(NAME posix COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/posix_tests.scm)
    set_tests_properties(posix PROPERTIES
        ENVIRONMENT "CURRY_MODULE_PATH=${MOD_PATH}")
    # (srfi s19 time) imports (curry posix) for current-time.
    add_test(NAME time_srfi COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/time_srfi_tests.scm)
    set_tests_properties(time_srfi PROPERTIES
        ENVIRONMENT "CURRY_MODULE_PATH=${MOD_PATH}")
    add_test(NAME srfi_170 COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/srfi_170_tests.scm)
    set_tests_properties(srfi_170 PROPERTIES
        ENVIRONMENT "CURRY_MODULE_PATH=${MOD_PATH}")
    # Also needs (curry posix), transitively via (srfi srfi-170).
    add_test(NAME srfi_legacy_dashed_names COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/srfi_legacy_dashed_names_tests.scm)
    set_tests_properties(srfi_legacy_dashed_names PROPERTIES
        ENVIRONMENT "CURRY_MODULE_PATH=${MOD_PATH}")
    # (curry okf) imports (curry posix) for directory walking and (srfi 19)
    # for staleness/log timestamps.
    add_test(NAME okf COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/okf_tests.scm)
    set_tests_properties(okf PROPERTIES
        ENVIRONMENT "CURRY_MODULE_PATH=${MOD_PATH}")

    # (curry airports) imports (curry posix) for create-directory/
    # rename-file/set-environment-variable! and (curry http) for the
    # cache-refresh download. airports_tests.scm redirects HOME to an
    # isolated fixture cache before importing the module, so it never
    # touches the network or the real user cache.
    if(BUILD_MODULE_HTTP AND TARGET curry_http)
        add_test(NAME airports COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/airports_tests.scm)
        set_tests_properties(airports PROPERTIES
            ENVIRONMENT "CURRY_MODULE_PATH=${MOD_PATH}")
    endif()
endif()

if(BUILD_MODULE_CODESETS AND TARGET curry_codesets)
    add_test(NAME codesets COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/codesets_tests.scm)
    set_tests_properties(codesets PROPERTIES
        ENVIRONMENT "CURRY_MODULE_PATH=${MOD_PATH}")
endif()

if(BUILD_MODULE_RPI AND TARGET curry_rpi)
    # rpi_tests.scm: hardware-free simulation tests + API surface check (Linux)
    add_test(NAME rpi COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/rpi_tests.scm)
    set_tests_properties(rpi PROPERTIES
        ENVIRONMENT "CURRY_MODULE_PATH=${MOD_PATH}")
    # test_rpi.scm: smoke tests that open real device nodes (skipped if absent)
    add_test(NAME rpi_hw COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/test_rpi.scm)
    set_tests_properties(rpi_hw PROPERTIES
        ENVIRONMENT "CURRY_MODULE_PATH=${MOD_PATH}")
endif()

if(BUILD_MODULE_TYPEDVEC AND TARGET curry_typedvec AND BUILD_MODULE_F64VECTOR AND TARGET curry_f64vector)
    # (srfi 4) imports both (curry typedvec) and (curry f64vector).
    add_test(NAME typedvec COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/typedvec_tests.scm)
    set_tests_properties(typedvec PROPERTIES
        ENVIRONMENT "CURRY_MODULE_PATH=${MOD_PATH}")
    # (srfi 160) builds on (srfi 4) in pure Scheme -- same module dependency.
    add_test(NAME srfi_160 COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/srfi_160_tests.scm)
    set_tests_properties(srfi_160 PROPERTIES
        ENVIRONMENT "CURRY_MODULE_PATH=${MOD_PATH}")
endif()

if(BUILD_MODULE_QT6 AND TARGET curry_qt6)
    # Phase 1 (widget creation) runs immediately; Phase 2 (painter / text
    # metrics) triggers an offscreen render via canvas-save-png!.
    # QT_QPA_PLATFORM=offscreen lets this run in CI without a display.
    add_test(NAME qt6 COMMAND curry --clear-cache ${CMAKE_CURRENT_SOURCE_DIR}/test_qt6_new.scm)
    set_tests_properties(qt6 PROPERTIES
        ENVIRONMENT "CURRY_MODULE_PATH=${MOD_PATH};QT_QPA_PLATFORM=offscreen"
        TIMEOUT 30)
endif()
