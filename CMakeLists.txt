cmake_minimum_required(VERSION 3.24)

project(KeyStats VERSION 1.0 LANGUAGES CXX)

if (NOT WIN32)
    message(FATAL_ERROR "KeyStats uses Win32 Raw Input and requires Windows.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Find SQLite without requiring a package manager-specific CMake config file.
# Set SQLite3_ROOT (or SQLITE3_ROOT) when SQLite is installed in a custom path.
set(_sqlite_roots)
if (EXISTS "${CMAKE_SOURCE_DIR}/third_party/sqlite/include/sqlite3.h")
    list(APPEND _sqlite_roots "${CMAKE_SOURCE_DIR}/third_party/sqlite")
elseif (DEFINED SQLite3_ROOT)
    list(APPEND _sqlite_roots "${SQLite3_ROOT}")
elseif (DEFINED SQLITE3_ROOT)
    list(APPEND _sqlite_roots "${SQLITE3_ROOT}")
endif()

find_path(SQLITE3_INCLUDE_DIR sqlite3.h
    HINTS ${_sqlite_roots}
    PATH_SUFFIXES include)
find_library(SQLITE3_LIBRARY NAMES sqlite3 sqlite3.lib
    HINTS ${_sqlite_roots}
    PATH_SUFFIXES lib lib/x64)

if (NOT SQLITE3_INCLUDE_DIR OR NOT SQLITE3_LIBRARY)
    message(FATAL_ERROR
        "SQLite3 was not found. Install the SQLite3 development package, "
        "or configure with -DSQLite3_ROOT=<directory containing include/ and lib/>.")
endif()

add_library(SQLite3::SQLite3 UNKNOWN IMPORTED)
set_target_properties(SQLite3::SQLite3 PROPERTIES
    IMPORTED_LOCATION "${SQLITE3_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${SQLITE3_INCLUDE_DIR}")

set(KEYSTATS_CORE
    src/core/key_catalog.cpp
    src/core/key_normalizer.cpp
    src/core/keyboard_counter.cpp
)

set(KEYSTATS_STORAGE
    src/storage/count_vector_codec.cpp
    src/storage/repository.cpp
    src/storage/settings_store.cpp
    src/storage/statistics_recorder.cpp
)

set(KEYSTATS_APP
    src/app/app.rc
    src/app/application.cpp
    src/app/custom_controls.cpp
    src/app/main.cpp
    src/app/raw_input.cpp
    src/app/startup.cpp
    src/app/tray.cpp
)

add_executable(KeyStats WIN32 ${KEYSTATS_CORE} ${KEYSTATS_STORAGE} ${KEYSTATS_APP})
target_include_directories(KeyStats PRIVATE src)
target_link_libraries(KeyStats PRIVATE SQLite3::SQLite3 comctl32 comdlg32 shell32 advapi32 gdi32 user32)
target_compile_definitions(KeyStats PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
if (MSVC)
    target_compile_options(KeyStats PRIVATE /utf-8)
endif()

add_executable(KeyStatsTests tests/test_main.cpp ${KEYSTATS_CORE} ${KEYSTATS_STORAGE})
target_include_directories(KeyStatsTests PRIVATE src)
target_link_libraries(KeyStatsTests PRIVATE SQLite3::SQLite3)
target_compile_definitions(KeyStatsTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
if (MSVC)
    target_compile_options(KeyStatsTests PRIVATE /utf-8)
endif()

enable_testing()
add_test(NAME KeyStatsTests COMMAND KeyStatsTests)
