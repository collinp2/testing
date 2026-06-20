cmake_minimum_required(VERSION 3.21)

# Build a universal binary on macOS (arm64 + x86_64) so hosts that run x86_64 —
# notably Adobe Premiere Pro / its audio plug-in scanner under Rosetta — can
# load the plugin. Set before project() so the toolchain and all FetchContent
# dependencies build for both slices. Override with -DCMAKE_OSX_ARCHITECTURES.
if(CMAKE_HOST_APPLE AND NOT DEFINED CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS universal build")
endif()

# ============================================================================
#  NECRONAM  —  Neural Amp Necromancy
#  A Neural Amp Modeler (NAM) capture player with cab IR, API-560 style
#  graphic EQ, hi/low-pass filters and the Flesh Render multiband saturator.
#  CP Software. JUCE + NeuralAmpModelerCore.
# ============================================================================

project(NECRONAM VERSION 1.0.0 LANGUAGES C CXX)

# NAM core requires C++20; JUCE is happy with it too.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# ----------------------------------------------------------------------------
# JUCE  (develop branch + full history — required for macOS 15, per repo notes)
# ----------------------------------------------------------------------------
FetchContent_Declare(
    juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        develop
    GIT_SHALLOW    FALSE
)
FetchContent_MakeAvailable(juce)

# ----------------------------------------------------------------------------
# NeuralAmpModelerCore  (model inference only). Eigen + nlohmann/json are
# vendored under its Dependencies. We compile its NAM/ sources directly and do
# NOT use AudioDSPTools (it is entangled with iPlug2 and hardcodes double);
# the gate, cab IR, DC blocker and resampling are implemented in JUCE instead.
# ----------------------------------------------------------------------------
FetchContent_Declare(
    nam_core
    GIT_REPOSITORY https://github.com/sdatkinson/NeuralAmpModelerCore.git
    GIT_TAG        main
    GIT_SHALLOW    FALSE
)
FetchContent_GetProperties(nam_core)
if(NOT nam_core_POPULATED)
    FetchContent_Populate(nam_core)
endif()

set(NAM_DIR  "${nam_core_SOURCE_DIR}")
set(NAM_DEPS "${NAM_DIR}/Dependencies")

file(GLOB NAM_CORE_SOURCES
    "${NAM_DIR}/NAM/*.cpp"
    "${NAM_DIR}/NAM/wavenet/*.cpp")

if(NOT NAM_CORE_SOURCES)
    message(FATAL_ERROR
        "NeuralAmpModelerCore sources not found. Run a fresh configure so "
        "FetchContent can clone the repo and its submodules (Eigen).")
endif()

# ----------------------------------------------------------------------------
# Plugin target
# ----------------------------------------------------------------------------
# Install system-wide on macOS (/Library) instead of the per-user folder.
# Guarded so non-macOS builds keep the default (per-user) copy location.
set(NECRONAM_COPY_ARGS COPY_PLUGIN_AFTER_BUILD TRUE)
if(APPLE)
    list(APPEND NECRONAM_COPY_ARGS
        VST3_COPY_DIR "/Library/Audio/Plug-Ins/VST3"
        AU_COPY_DIR   "/Library/Audio/Plug-Ins/Components")
endif()

juce_add_plugin(NECRONAM
    PRODUCT_NAME              "NECRONAM"
    COMPANY_NAME             "CP Software"
    BUNDLE_ID                "com.cpsoftware.necronam"
    DESCRIPTION              "Neural Amp Necromancy — NAM player, IR, EQ & Saturation"
    PLUGIN_MANUFACTURER_CODE  Cpsw
    PLUGIN_CODE               Ncnm
    FORMATS                   VST3 AU Standalone
    IS_SYNTH                  FALSE
    NEEDS_MIDI_INPUT          FALSE
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    ${NECRONAM_COPY_ARGS}
    VST3_CATEGORIES           "Fx" "Distortion"
    AU_MAIN_TYPE              "kAudioUnitType_Effect"
)

juce_generate_juce_header(NECRONAM)

# Embedded assets (logo + body typefaces).
juce_add_binary_data(NecronamAssets SOURCES
    Resources/Anton-Regular.ttf
    Resources/IBMPlexMono-Regular.ttf
    Resources/IBMPlexMono-Bold.ttf)

target_sources(NECRONAM PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/HorrorLookAndFeel.cpp
    ${NAM_CORE_SOURCES}
)

# NAM core processes float buffers (matches JUCE).
target_compile_definitions(NECRONAM PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    NAM_SAMPLE_FLOAT=1
)

# Include paths for NAM core + its vendored nlohmann/json and Eigen.
target_include_directories(NECRONAM PRIVATE
    "${NAM_DIR}"
    "${NAM_DEPS}"
    "${NAM_DEPS}/nlohmann"
)
target_include_directories(NECRONAM SYSTEM PRIVATE
    "${NAM_DEPS}/eigen"
)

target_link_libraries(NECRONAM PRIVATE
    NecronamAssets
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)
