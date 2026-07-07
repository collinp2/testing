cmake_minimum_required(VERSION 3.15)
project(FleshRender VERSION 2.0.0 LANGUAGES C CXX OBJCXX)

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

# ---- AAX SDK -------------------------------------------------------------------
set(AAX_SDK_PATH "$ENV{HOME}/Downloads/aax-sdk-2-9-0" CACHE PATH "Path to Avid AAX SDK")

juce_set_aax_sdk_path("${AAX_SDK_PATH}")

# Build only the AAXLibrary, skip examples and TI interface
set(AAX_BUILD_EXAMPLES       OFF CACHE BOOL "" FORCE)
set(AAX_BUILD_TI_INTERFACE   OFF CACHE BOOL "" FORCE)
set(AAX_BUILD_JUCE_GUI_EXTENSION OFF CACHE BOOL "" FORCE)
add_subdirectory("${AAX_SDK_PATH}" "${CMAKE_BINARY_DIR}/aax-sdk" EXCLUDE_FROM_ALL)

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
    FORMATS                     VST3 AU Standalone AAX
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

target_link_libraries(FleshRender
    PRIVATE
        AAXLibrary
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
