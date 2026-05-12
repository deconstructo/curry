cmake_minimum_required(VERSION 3.20)
project(curry VERSION 0.7.5 LANGUAGES C CXX)

include(GNUInstallDirs)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_compile_options(-g -O0)
endif()
if(CMAKE_BUILD_TYPE STREQUAL "Release")
  add_compile_options(-O2)
endif()

# On macOS, prefer the Homebrew arm64 OpenSSL over any x86_64 copy in /usr/local.
if(APPLE)
  if(NOT OPENSSL_ROOT_DIR)
    execute_process(
      COMMAND brew --prefix openssl
      OUTPUT_VARIABLE OPENSSL_ROOT_DIR
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET)
  endif()
  # Clear stale cache entries that point outside the desired root (e.g. a
  # previous configure found the x86_64 copy in /usr/local on an arm64 host).
  if(OPENSSL_ROOT_DIR AND OPENSSL_SSL_LIBRARY AND
     NOT OPENSSL_SSL_LIBRARY MATCHES "^${OPENSSL_ROOT_DIR}")
    message(STATUS "Clearing stale OpenSSL cache: ${OPENSSL_SSL_LIBRARY}")
    unset(OPENSSL_CRYPTO_LIBRARY CACHE)
    unset(OPENSSL_SSL_LIBRARY CACHE)
    unset(OPENSSL_INCLUDE_DIR CACHE)
  endif()
endif()

# ---- Required dependencies ----

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)

# Boehm GC — the .pc file may live in /usr/local/lib/pkgconfig which CMake
# doesn't search by default.  Extend PKG_CONFIG_PATH then try pkg-config,
# with a find_library fallback for systems that have the lib but no .pc file.
if(NOT "$ENV{PKG_CONFIG_PATH}" STREQUAL "")
  set(ENV{PKG_CONFIG_PATH} "$ENV{PKG_CONFIG_PATH}:/usr/local/lib/pkgconfig")
else()
  set(ENV{PKG_CONFIG_PATH} "/usr/local/lib/pkgconfig")
endif()
pkg_check_modules(GC QUIET bdw-gc)

if(NOT GC_FOUND)
  find_library(GC_LIB_MANUAL NAMES gc
               HINTS /usr/local/lib /usr/lib
               PATH_SUFFIXES lib64 lib)
  find_path(GC_INCLUDE_MANUAL NAMES gc/gc.h gc.h
            HINTS /usr/local/include /usr/include)
  if(GC_LIB_MANUAL AND GC_INCLUDE_MANUAL)
    message(STATUS "Boehm GC found via find_library: ${GC_LIB_MANUAL}")
    set(GC_LIBRARIES    "${GC_LIB_MANUAL}")
    set(GC_INCLUDE_DIRS "${GC_INCLUDE_MANUAL}")
    set(GC_CFLAGS_OTHER "")
  else()
    message(FATAL_ERROR
      "Boehm GC not found.\n"
      "  Ubuntu/Debian : sudo apt install libgc-dev\n"
      "  macOS (brew)  : brew install bdw-gc\n"
      "  Other         : set PKG_CONFIG_PATH if installed to a custom prefix.")
  endif()
endif()

# GMP (arbitrary precision)
find_library(GMP_LIB NAMES gmp REQUIRED)
find_path(GMP_INCLUDE NAMES gmp.h REQUIRED)

# readline — optional, enhances the REPL with history and line editing
find_library(READLINE_LIB NAMES readline)
find_path(READLINE_INCLUDE NAMES readline/readline.h)

# ---- Core library ----

set(CORE_SOURCES
  src/gc.c
  src/symbol.c
  src/numeric.c
  src/port.c
  src/reader.c
  src/env.c
  src/eval.c
  src/builtins.c
  src/set.c
  src/actors.c
  src/modules.c
  src/symbolic.c
  src/symbolic_print.c
  src/quantum.c
  src/surreal.c
  src/multivec.c
  src/matrix.c
  src/syntax_rules.c
)

add_library(curry_core STATIC ${CORE_SOURCES})
set_target_properties(curry_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(curry_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${GC_INCLUDE_DIRS}
  ${GMP_INCLUDE}
)
target_link_libraries(curry_core PUBLIC
  ${GC_LIBRARIES}
  ${GMP_LIB}
  Threads::Threads
  ${CMAKE_DL_LIBS}
  m
)
target_compile_options(curry_core PUBLIC ${GC_CFLAGS_OTHER})
if(GC_LIBRARY_DIRS)
  target_link_directories(curry_core PUBLIC ${GC_LIBRARY_DIRS})
endif()

# Main REPL executable
# api.c is compiled directly into the binary (not only into curry_core.a) so
# that --export-dynamic actually exports all curry_* public API symbols.
# Static archive members are only linked when referenced; direct object files
# are always included.
add_executable(curry src/main.c src/api.c)
target_link_libraries(curry PRIVATE curry_core)
# Export all curry_core symbols so dynamically-loaded modules can resolve them
# without linking their own private copy of curry_core (which would give each
# module a separate symbol-intern table, breaking eq? across module boundaries).
set_target_properties(curry PROPERTIES ENABLE_EXPORTS ON)

if(READLINE_LIB AND READLINE_INCLUDE)
  target_link_libraries(curry PRIVATE ${READLINE_LIB})
  target_compile_definitions(curry PRIVATE HAVE_READLINE)
  target_include_directories(curry PRIVATE ${READLINE_INCLUDE})
  message(STATUS "readline found — REPL will use readline")
else()
  message(STATUS "readline not found — REPL uses basic line editing")
endif()

# ---- Optional modules (shared libraries loaded at runtime) ----

option(BUILD_MODULE_JSON      "Build JSON module"                  ON)
option(BUILD_MODULE_SQLITE    "Build SQLite module"                ON)
option(BUILD_MODULE_NETWORK   "Build network module"               ON)
option(BUILD_MODULE_CRYPTO    "Build crypto module (base64/MD5/SHA)"ON)
option(BUILD_MODULE_LDAP      "Build LDAP/LDAPS module"            ON)
option(BUILD_MODULE_STORAGE   "Build object storage module (S3/GCS/Azure/Swift)" ON)
option(BUILD_MODULE_GRAPHQL   "Build GraphQL client module"         OFF)
option(BUILD_MODULE_SYMENGINE "Build SymEngine module"              OFF)
option(BUILD_MODULE_NEO4J     "Build Neo4j module"                  OFF)
option(BUILD_MODULE_REDIS     "Build Redis client module"           ON)
option(BUILD_MODULE_MQTT      "Build MQTT client module (Paho C)"   ON)
option(BUILD_MODULE_IMAGE     "Build PNG/JPEG/GIF image module"     ON)
option(BUILD_MODULE_GIT       "Build Git module (libgit2)"          ON)
option(BUILD_MODULE_VECDB     "Build vector DB module"              OFF)
option(BUILD_MODULE_QT6       "Build Qt6 graphics module"           OFF)
option(BUILD_MODULE_PLPLOT    "Build PLplot scientific plotting module" ON)
option(BUILD_MODULE_REGEX     "Build POSIX regex module"            ON)
option(BUILD_MODULE_SYNC      "Build mutex/condvar/semaphore module" ON)
option(BUILD_MODULE_MCP       "Build MCP server module"              ON)

# Modules are loaded as (curry name), which maps to the path "curry/name.so".
# So the output file must be named "name.so" and placed in a "curry/" subdirectory
# of the module search path.  At build time we put them in build/curry/.

# Modules are loaded as (curry name) → path "curry/name.so".
# In the build tree we place them in build/mods/curry/ so the default
# search dir "mods" finds them.  Installed files go to lib/curry/modules/curry/.

file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/mods/curry)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/mods/curry/math)

# Sub-module helper for (curry math <name>) → curry/math/<name>.so
function(curry_math_module name)
  add_library(curry_math_${name} MODULE modules/math/${name}.c)
  if(APPLE)
    target_link_options(curry_math_${name} PRIVATE -undefined dynamic_lookup)
  endif()
  target_include_directories(curry_math_${name} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${GC_INCLUDE_DIRS}
    ${GMP_INCLUDE}
  )
  set_target_properties(curry_math_${name} PROPERTIES
    PREFIX ""
    OUTPUT_NAME "${name}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/mods/curry/math"
  )
  install(TARGETS curry_math_${name} DESTINATION lib/curry/modules/curry/math)
endfunction()

curry_math_module(matrix)
curry_math_module(tensor)

function(curry_c_module name)
  add_library(curry_${name} MODULE modules/${name}/${name}.c)
  # Do NOT link curry_core here — modules resolve curry_core symbols from the
  # main binary at dlopen time (main binary is built with -rdynamic).  Linking
  # curry_core privately would embed a duplicate symbol-intern table, causing
  # symbols created by the module to be pointer-inequal to the same symbols in
  # the main binary and breaking frame_lookup.
  # On macOS the static linker rejects unresolved symbols by default; tell it
  # to defer resolution to dlopen time (same effect as -rdynamic on Linux).
  if(APPLE)
    target_link_options(curry_${name} PRIVATE -undefined dynamic_lookup)
  endif()
  target_include_directories(curry_${name} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${GC_INCLUDE_DIRS}
    ${GMP_INCLUDE}
  )
  set_target_properties(curry_${name} PROPERTIES
    PREFIX ""
    OUTPUT_NAME "${name}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/mods/curry"
  )
  install(TARGETS curry_${name} DESTINATION lib/curry/modules/curry)
endfunction()

function(curry_cxx_module name)
  add_library(curry_${name} MODULE modules/${name}/${name}.cpp)
  # Same rationale as curry_c_module: no curry_core link; resolved from main binary.
  if(APPLE)
    target_link_options(curry_${name} PRIVATE -undefined dynamic_lookup)
  endif()
  target_include_directories(curry_${name} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${GC_INCLUDE_DIRS}
    ${GMP_INCLUDE}
  )
  set_target_properties(curry_${name} PROPERTIES
    PREFIX ""
    OUTPUT_NAME "${name}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/mods/curry"
  )
  install(TARGETS curry_${name} DESTINATION lib/curry/modules/curry)
endfunction()

if(BUILD_MODULE_JSON)
  curry_c_module(json)
endif()

if(BUILD_MODULE_SQLITE)
  find_package(SQLite3 QUIET)
  if(SQLite3_FOUND)
    curry_c_module(sqlite)
    target_link_libraries(curry_sqlite PRIVATE SQLite::SQLite3)
  else()
    message(WARNING "SQLite3 not found, skipping sqlite module")
  endif()
endif()

if(BUILD_MODULE_NETWORK)
  curry_c_module(network)
endif()

if(BUILD_MODULE_CRYPTO)
  find_package(OpenSSL QUIET)
  if(OpenSSL_FOUND)
    curry_c_module(crypto)
    target_link_libraries(curry_crypto PRIVATE OpenSSL::Crypto)
  else()
    message(WARNING "OpenSSL not found, skipping crypto module")
  endif()
endif()

if(BUILD_MODULE_LDAP)
  pkg_check_modules(LDAP QUIET ldap)
  if(LDAP_FOUND)
    curry_c_module(ldap)
    target_link_libraries(curry_ldap PRIVATE ${LDAP_LIBRARIES})
    target_include_directories(curry_ldap PRIVATE ${LDAP_INCLUDE_DIRS})
  else()
    find_library(LDAP_LIB NAMES ldap)
    if(LDAP_LIB)
      curry_c_module(ldap)
      target_link_libraries(curry_ldap PRIVATE ${LDAP_LIB})
    else()
      message(WARNING "libldap not found, skipping ldap module")
    endif()
  endif()
endif()


if(BUILD_MODULE_STORAGE)
  find_package(CURL QUIET)
  find_package(OpenSSL QUIET)
  if(CURL_FOUND AND OpenSSL_FOUND)
    curry_c_module(storage)
    target_link_libraries(curry_storage PRIVATE CURL::libcurl OpenSSL::Crypto)
  else()
    message(WARNING "libcurl or OpenSSL not found — run: sudo apt install libcurl4-openssl-dev")
  endif()
endif()

if(BUILD_MODULE_GRAPHQL)
  find_package(CURL QUIET)
  if(CURL_FOUND)
    curry_c_module(graphql)
    target_link_libraries(curry_graphql PRIVATE CURL::libcurl)
  else()
    message(WARNING "libcurl not found — run: sudo apt install libcurl4-openssl-dev")
  endif()
endif()

if(BUILD_MODULE_SYMENGINE)
  find_package(SymEngine QUIET)
  if(SymEngine_FOUND)
    curry_cxx_module(symengine)
    target_link_libraries(curry_symengine PRIVATE symengine)
  else()
    message(WARNING "SymEngine not found, skipping symengine module")
  endif()
endif()

if(BUILD_MODULE_NEO4J)
  curry_c_module(neo4j)
endif()

if(BUILD_MODULE_REDIS)
  curry_c_module(redis)
  find_package(OpenSSL QUIET)
  if(OpenSSL_FOUND)
    target_compile_definitions(curry_redis PRIVATE HAVE_REDIS_TLS)
    target_link_libraries(curry_redis PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    message(STATUS "Redis TLS support enabled (OpenSSL ${OPENSSL_VERSION})")
  else()
    message(STATUS "Redis TLS support disabled (OpenSSL not found)")
  endif()
endif()

if(BUILD_MODULE_MQTT)
  find_path(PAHO_INCLUDE_DIR MQTTClient.h
    HINTS /opt/homebrew/include /usr/local/include /usr/include)
  find_library(PAHO_CS_LIB NAMES paho-mqtt3cs
    HINTS /opt/homebrew/lib /usr/local/lib /usr/lib)
  find_library(PAHO_C_LIB NAMES paho-mqtt3c
    HINTS /opt/homebrew/lib /usr/local/lib /usr/lib)
  if(PAHO_INCLUDE_DIR AND (PAHO_CS_LIB OR PAHO_C_LIB))
    curry_c_module(mqtt)
    target_include_directories(curry_mqtt PRIVATE ${PAHO_INCLUDE_DIR})
    if(PAHO_CS_LIB)
      target_link_libraries(curry_mqtt PRIVATE ${PAHO_CS_LIB} Threads::Threads)
      target_compile_definitions(curry_mqtt PRIVATE HAVE_MQTT_TLS)
      message(STATUS "MQTT module enabled with TLS (libpaho-mqtt3cs)")
    else()
      target_link_libraries(curry_mqtt PRIVATE ${PAHO_C_LIB} Threads::Threads)
      message(STATUS "MQTT module enabled without TLS (libpaho-mqtt3c)")
    endif()
  else()
    message(STATUS "MQTT module disabled — install: brew install libpaho-mqtt / sudo apt install libpaho-mqtt-dev")
  endif()
endif()

if(BUILD_MODULE_IMAGE)
  find_package(PNG QUIET)
  find_package(JPEG QUIET)
  if(PNG_FOUND OR JPEG_FOUND)
    curry_c_module(image)
    if(PNG_FOUND)
      target_link_libraries(curry_image PRIVATE PNG::PNG)
      target_compile_definitions(curry_image PRIVATE HAVE_PNG)
    endif()
    if(JPEG_FOUND)
      target_link_libraries(curry_image PRIVATE JPEG::JPEG)
      target_compile_definitions(curry_image PRIVATE HAVE_JPEG)
    endif()
  else()
    message(WARNING "libpng and libjpeg not found, skipping image module")
  endif()
endif()

if(BUILD_MODULE_GIT)
  pkg_check_modules(LIBGIT2 QUIET libgit2)
  if(LIBGIT2_FOUND)
    curry_c_module(git)
    target_link_libraries(curry_git PRIVATE ${LIBGIT2_LIBRARIES})
    target_include_directories(curry_git PRIVATE ${LIBGIT2_INCLUDE_DIRS})
    if(LIBGIT2_LIBRARY_DIRS)
      target_link_directories(curry_git PRIVATE ${LIBGIT2_LIBRARY_DIRS})
    endif()
  else()
    find_library(LIBGIT2_LIB NAMES git2)
    find_path(LIBGIT2_INCLUDE NAMES git2.h)
    if(LIBGIT2_LIB AND LIBGIT2_INCLUDE)
      curry_c_module(git)
      target_link_libraries(curry_git PRIVATE ${LIBGIT2_LIB})
      target_include_directories(curry_git PRIVATE ${LIBGIT2_INCLUDE})
    else()
      message(WARNING "libgit2 not found, skipping git module")
    endif()
  endif()
endif()

if(BUILD_MODULE_VECDB)
  # Uses usearch or hnswlib via C wrapper
  curry_cxx_module(vecdb)
endif()

if(BUILD_MODULE_QT6)
  find_package(Qt6 QUIET COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets)
  if(Qt6_FOUND)
    curry_cxx_module(qt6)
    set_target_properties(curry_qt6 PROPERTIES AUTOMOC ON)
    target_link_libraries(curry_qt6 PRIVATE
      Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets)
    message(STATUS "Qt6 ${Qt6_VERSION} found — building qt6 module")
  else()
    message(WARNING "Qt6 not found, skipping qt6 module")
  endif()
endif()

if(BUILD_MODULE_PLPLOT)
  pkg_check_modules(PLPLOT QUIET plplot)
  if(PLPLOT_FOUND)
    curry_c_module(plplot)
    target_include_directories(curry_plplot PRIVATE ${PLPLOT_INCLUDE_DIRS})
    target_link_directories(curry_plplot PRIVATE ${PLPLOT_LIBRARY_DIRS})
    target_link_libraries(curry_plplot PRIVATE ${PLPLOT_LIBRARIES})
    target_compile_options(curry_plplot PRIVATE ${PLPLOT_CFLAGS_OTHER})
    message(STATUS "PLplot ${PLPLOT_VERSION} found — building plplot module")
  else()
    message(WARNING "PLplot not found, skipping plplot module (Linux: sudo apt install libplplot-dev  |  macOS: brew install plplot)")
  endif()
endif()

if(BUILD_MODULE_REGEX)
  curry_c_module(regex)
  message(STATUS "Building regex module (system POSIX regex.h)")
endif()

if(BUILD_MODULE_SYNC)
  curry_c_module(sync)
  message(STATUS "Building sync module (pthreads mutex/condvar/semaphore)")
endif()

if(BUILD_MODULE_MCP)
  curry_c_module(mcp)
  message(STATUS "Building MCP server module")
endif()

# ---- Tests ----

enable_testing()
add_subdirectory(tests)

# ---- Install ----

install(TARGETS curry DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS curry_core DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/curry)
# Scheme source modules (gravity.scm etc.) — placed so <exe>/../lib/curry/modules finds them
install(DIRECTORY lib/curry/modules/
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/curry/modules
        OPTIONAL
        FILES_MATCHING PATTERN "*.scm")

# Documentation
install(DIRECTORY docs/
        DESTINATION ${CMAKE_INSTALL_DOCDIR}
        FILES_MATCHING PATTERN "*.md" PATTERN "*.pdf")
install(FILES README.md LICENSE
        DESTINATION ${CMAKE_INSTALL_DOCDIR})

# ---- Packaging (CPack / .deb) ----

set(CPACK_PACKAGE_NAME              "curry-scheme")
set(CPACK_PACKAGE_VENDOR            "Chris Ó Luanaigh")
set(CPACK_PACKAGE_CONTACT           "metanoia@gmail.com")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Curry Scheme — R7RS interpreter with actor concurrency and extended numerics")
set(CPACK_PACKAGE_DESCRIPTION "\
R7RS Scheme interpreter featuring Boehm GC, GMP arbitrary-precision\
 arithmetic, actor-model concurrency (pthreads), Clifford algebra\
 multivectors, and a built-in computer algebra system with symbolic\
 differentiation, integration (definite and indefinite), polynomial\
 operations (expand, collect, degree), Wirtinger calculus, fractional\
 calculus, and a full transcendental function library. Rich optional\
 module system: JSON, SQLite, Redis, S3/Azure/GCS, GraphQL, Git,\
 image I/O, Qt6, PLplot.")
set(CPACK_RESOURCE_FILE_LICENSE     "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README      "${CMAKE_CURRENT_SOURCE_DIR}/README.md")

# DEB-specific
set(CPACK_GENERATOR                 "DEB")
set(CPACK_DEBIAN_FILE_NAME          DEB-DEFAULT)   # curry-scheme_0.7.5_<arch>.deb
set(CPACK_PACKAGING_INSTALL_PREFIX  "/usr")
set(CPACK_DEBIAN_PACKAGE_SECTION    "interpreters")
set(CPACK_DEBIAN_PACKAGE_PRIORITY   "optional")

# Hard runtime deps — always required regardless of which modules are built
set(CPACK_DEBIAN_PACKAGE_DEPENDS
    "libgc1t64 | libgc1 | libgc2, libgmp10")

# Always-on optional modules (network, redis, sqlite, regex, sync, vecdb, json, mqtt)
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS
    "libsqlite3-0, \
     libssl3 | libssl1.1, \
     libcurl4 | libcurl4t64, \
     libgit2-1.1 | libgit2-1.5 | libgit2-1.7 | libgit2-1.9, \
     libpng16-16 | libpng16-16t64, \
     libjpeg-turbo8 | libjpeg62-turbo | libjpeg-turbo9, \
     libpaho-mqtt3cs1 | libpaho-mqtt3c1, \
     libldap-2.5-0 | libldap-2.4-2")

# Heavy optional modules — useful but not installed by default
set(CPACK_DEBIAN_PACKAGE_SUGGESTS
    "libqt6widgets6, libqt6opengl6, \
     libplplot19 | libplplot17, \
     libreadline8 | libreadline9")

include(CPack)
