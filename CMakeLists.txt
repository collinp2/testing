cmake_minimum_required(VERSION 3.20)
project(GEQ12 VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(GEQ12_BUILD_AAX   "Build AAX plugin (requires AAX SDK)" OFF)
option(GEQ12_BUILD_TESTS "Build unit tests" ON)
set(AAX_SDK_PATH "" CACHE PATH "Path to the Avid AAX SDK")

# ---------------------------------------------------------------------------
# DSP library (header-only, but we create an INTERFACE target for convenience)
# ---------------------------------------------------------------------------
add_library(geq12_dsp INTERFACE)
target_include_directories(geq12_dsp INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# ---------------------------------------------------------------------------
# AAX Plugin target
# ---------------------------------------------------------------------------
if(GEQ12_BUILD_AAX)
    if(NOT EXISTS "${AAX_SDK_PATH}/Interfaces/AAX.h")
        message(FATAL_ERROR
            "AAX SDK not found at '${AAX_SDK_PATH}'. "
            "Set -DAAX_SDK_PATH=<path-to-aax-sdk> or disable with -DGEQ12_BUILD_AAX=OFF")
    endif()

    # The AAX SDK provides its own CMake support in newer versions.
    # For older SDK versions, add include/lib paths manually.
    add_library(geq12_aax MODULE
        src/aax/GEQ12_Describe.cpp
    )
    target_link_libraries(geq12_aax PRIVATE geq12_dsp)
    target_compile_definitions(geq12_aax PRIVATE HAS_AAX_SDK=1)
    target_include_directories(geq12_aax PRIVATE "${AAX_SDK_PATH}/Interfaces")

    # On macOS, AAX plugins use .aaxplugin bundle extension
    if(APPLE)
        set_target_properties(geq12_aax PROPERTIES
            BUNDLE TRUE
            BUNDLE_EXTENSION "aaxplugin"
            OUTPUT_NAME "GEQ-12"
        )
    elseif(WIN32)
        set_target_properties(geq12_aax PROPERTIES
            SUFFIX ".aaxplugin"
            OUTPUT_NAME "GEQ-12"
        )
    endif()
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(GEQ12_BUILD_TESTS)
    enable_testing()
    add_executable(geq12_tests
        tests/test_biquad.cpp
        tests/test_graphic_eq.cpp
        tests/test_plugin_processor.cpp
        tests/test_main.cpp
    )
    target_link_libraries(geq12_tests PRIVATE geq12_dsp)

    add_test(NAME GEQ12Tests COMMAND geq12_tests)
endif()
