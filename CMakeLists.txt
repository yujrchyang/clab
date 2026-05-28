cmake_minimum_required (VERSION 3.5)
project(clab VERSION 0.1.0 LANGUAGES CXX C)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "This project only supports Linux. Detected system: ${CMAKE_SYSTEM_NAME}")
endif()

# --- Global compiler settings ---
find_program(CCACHE ccache)
if(CCACHE)
    set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE})
    set(CMAKE_C_COMPILER_LAUNCHER ${CCACHE})
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

# --- Feature detection ---
include(DetectSIMD)
include(ConfigureChecks)

# --- Testing ---
# Always enable CTest so `make test` is always available
enable_testing()

# --- Build options: -DTEST=ON/OFF controls test compilation ---
enable_testing()
option(TEST "Build and run tests" ON)
if(TEST)
    add_subdirectory(tests)
endif()

# --- Third-party libraries (Git Submodules) ---
include(BuildGoogleTest)
include(BuildSpdlog)
include(BuildISAL)

# --- Common library ---
add_subdirectory(common)
