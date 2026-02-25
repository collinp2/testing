cmake_minimum_required(VERSION 3.15)
project(FleshRender VERSION 1.0.0)

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
    FORMATS                     VST3 Standalone
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
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
