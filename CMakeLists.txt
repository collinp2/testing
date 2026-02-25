cmake_minimum_required(VERSION 3.22)
project(GEQ12 VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(GEQ12_BUILD_PLUGIN "Build VST3 plugin (requires JUCE)" ON)
option(GEQ12_BUILD_TESTS  "Build DSP unit tests (no JUCE needed)" ON)
set(JUCE_PATH "" CACHE PATH "Path to JUCE framework (or use FetchContent)")

# ---------------------------------------------------------------------------
# JUCE — fetch automatically if no local path is provided
# ---------------------------------------------------------------------------
if(GEQ12_BUILD_PLUGIN)
    if(JUCE_PATH AND EXISTS "${JUCE_PATH}/CMakeLists.txt")
        add_subdirectory("${JUCE_PATH}" juce_build)
    else()
        include(FetchContent)
        FetchContent_Declare(
            juce
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG        develop
            GIT_SHALLOW    FALSE
        )
        FetchContent_MakeAvailable(juce)
    endif()

    # -------------------------------------------------------------------
    # VST3 Plugin
    # -------------------------------------------------------------------
    juce_add_plugin(GEQ12
        PRODUCT_NAME           "GEQ-12"
        COMPANY_NAME           "AudioTools"
        PLUGIN_MANUFACTURER_CODE Atls
        PLUGIN_CODE            Gq12
        FORMATS                VST3            # VST3 only
        IS_SYNTH               FALSE
        NEEDS_MIDI_INPUT       FALSE
        NEEDS_MIDI_OUTPUT      FALSE
        IS_MIDI_EFFECT         FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        VST3_CATEGORIES        "Eq"
    )

    target_sources(GEQ12
        PRIVATE
            Source/PluginProcessor.cpp
            Source/PluginEditor.cpp
    )

    target_include_directories(GEQ12
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/Source
    )

    target_compile_definitions(GEQ12
        PUBLIC
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_VST3_CAN_REPLACE_VST2=0
            JUCE_DISPLAY_SPLASH_SCREEN=0
    )

    target_link_libraries(GEQ12
        PRIVATE
            juce::juce_audio_processors
            juce::juce_audio_utils
            juce::juce_gui_basics
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags
    )
endif()

# ---------------------------------------------------------------------------
# DSP unit tests — standalone, no JUCE dependency
# ---------------------------------------------------------------------------
if(GEQ12_BUILD_TESTS)
    # Header-only DSP library
    add_library(geq12_dsp INTERFACE)
    target_include_directories(geq12_dsp INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/Source)

    enable_testing()
    add_executable(geq12_tests
        tests/test_biquad.cpp
        tests/test_graphic_eq.cpp
        tests/test_main.cpp
    )
    target_link_libraries(geq12_tests PRIVATE geq12_dsp)

    add_test(NAME GEQ12Tests COMMAND geq12_tests)
endif()
