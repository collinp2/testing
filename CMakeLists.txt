cmake_minimum_required(VERSION 3.15)

# Universal binary on macOS (arm64 + x86_64) unless the caller overrides —
# release builds must run on both Apple Silicon and Intel.
if(CMAKE_HOST_APPLE AND NOT DEFINED CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS universal build")
endif()

project(FleshRender VERSION 2.0.0 LANGUAGES C CXX)

# Objective-C++ only exists on Apple toolchains (needed by the JUCE/AAX
# wrappers there); declaring it unconditionally breaks Windows CI.
if(APPLE)
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)

# ---- Fetch JUCE ----------------------------------------------------------------
include(FetchContent)

FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        develop
    GIT_SHALLOW    FALSE
)
FetchContent_MakeAvailable(JUCE)

# ---- AAX (native) ----------------------------------------------------------------
# Enabled only when the Avid AAX SDK is present locally, so CI machines
# without the SDK still build VST3 / AU / Standalone.
set(AAX_SDK_PATH "$ENV{HOME}/Downloads/aax-sdk-2-9-0" CACHE PATH "Path to Avid AAX SDK")
set(FR_FORMATS VST3 AU Standalone)
if(EXISTS "${AAX_SDK_PATH}")
    juce_set_aax_sdk_path("${AAX_SDK_PATH}")
    # Build only the AAXLibrary, skip examples and TI interface
    set(AAX_BUILD_EXAMPLES       OFF CACHE BOOL "" FORCE)
    set(AAX_BUILD_TI_INTERFACE   OFF CACHE BOOL "" FORCE)
    set(AAX_BUILD_JUCE_GUI_EXTENSION OFF CACHE BOOL "" FORCE)
    add_subdirectory("${AAX_SDK_PATH}" "${CMAKE_BINARY_DIR}/aax-sdk" EXCLUDE_FROM_ALL)
    list(APPEND FR_FORMATS AAX)
    message(STATUS "Flesh Render: AAX SDK found - building AAX (native)")
else()
    message(STATUS "Flesh Render: AAX SDK not found at ${AAX_SDK_PATH} - skipping AAX")
endif()

# ---- Plugin target -------------------------------------------------------------
juce_add_plugin(FleshRender
    COMPANY_NAME                "VoidCraft Audio"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    PLUGIN_MANUFACTURER_CODE    Void
    PLUGIN_CODE                 Flsh
    FORMATS                     ${FR_FORMATS}
    AU_MAIN_TYPE                "kAudioUnitType_Effect"
    PRODUCT_NAME                "Flesh Render"
    DESCRIPTION                 "Multiband Saturation, Distortion & Fuzz"
    BUNDLE_ID                   "com.voidcraftaudio.fleshrender"
)

juce_generate_juce_header(FleshRender)

target_sources(FleshRender PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/BandPanel.cpp
    Source/HorrorLookAndFeel.cpp
)

target_include_directories(FleshRender PRIVATE Source)

target_compile_definitions(FleshRender PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
)

if(EXISTS "${AAX_SDK_PATH}")
    target_link_libraries(FleshRender PRIVATE AAXLibrary)
endif()

target_link_libraries(FleshRender
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
