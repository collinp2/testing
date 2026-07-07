cmake_minimum_required(VERSION 3.21)

# Build a universal binary on macOS (arm64 + x86_64) so hosts that run x86_64 —
# notably Adobe Premiere Pro / its audio plug-in scanner under Rosetta — can
# load the plugin. Set before project() so the toolchain and all FetchContent
# dependencies build for both slices. Override with -DCMAKE_OSX_ARCHITECTURES.
if(CMAKE_HOST_APPLE AND NOT DEFINED CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS universal build")
endif()

# ============================================================================
#  NECRONAM MAX  —  Neural Amp Necromancy (feature-loaded edition)
#  A two-amp NAM capture player: dual NAM models routed in series or parallel,
#  a stereo dual-IR cab mixer, an API-560 style graphic EQ, hi/low-pass
#  filters and the Flesh Render multiband saturator — all in a true-stereo
#  signal path, with an on-disk preset browser.
#
#  This is a SEPARATE plugin from NECRONAM (distinct name / code / bundle id)
#  so both can be installed and loaded side by side.
#  CP Software. JUCE + NeuralAmpModelerCore.
# ============================================================================

# OBJCXX is required for the AAX SDK's Objective-C++ sources on macOS.
project(NECRONAM_MAX VERSION 2.2.1 LANGUAGES C CXX OBJCXX)

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
# AAX (native) — enabled only when the Avid AAX SDK is present locally, so CI
# machines without the SDK still build VST3 / AU / Standalone. Unsigned AAX
# loads in Pro Tools Developer; retail Pro Tools needs PACE signing once
# registered with Avid. (AAX DSP — the HDX TI-chip flavour — is not possible:
# JUCE has no DSP support and NAM inference can't run on those chips.)
# ----------------------------------------------------------------------------
set(AAX_SDK_PATH "$ENV{HOME}/Downloads/aax-sdk-2-9-0" CACHE PATH "Path to Avid AAX SDK")
set(NMAX_FORMATS VST3 AU Standalone)
if(EXISTS "${AAX_SDK_PATH}")
    juce_set_aax_sdk_path("${AAX_SDK_PATH}")
    set(AAX_BUILD_EXAMPLES         OFF CACHE BOOL "" FORCE)
    set(AAX_BUILD_TI_INTERFACE     OFF CACHE BOOL "" FORCE)
    set(AAX_BUILD_JUCE_GUI_EXTENSION OFF CACHE BOOL "" FORCE)
    add_subdirectory("${AAX_SDK_PATH}" "${CMAKE_BINARY_DIR}/aax-sdk" EXCLUDE_FROM_ALL)
    list(APPEND NMAX_FORMATS AAX)
    message(STATUS "NECRONAM MAX: AAX SDK found - building AAX (native)")
else()
    message(STATUS "NECRONAM MAX: AAX SDK not found at ${AAX_SDK_PATH} - skipping AAX")
endif()

# ----------------------------------------------------------------------------
# Plugin target
# ----------------------------------------------------------------------------
# Install system-wide on macOS (/Library) instead of the per-user folder.
# Guarded so non-macOS builds keep the default (per-user) copy location.
set(NMAX_COPY_ARGS COPY_PLUGIN_AFTER_BUILD TRUE)
if(APPLE)
    list(APPEND NMAX_COPY_ARGS
        VST3_COPY_DIR "/Library/Audio/Plug-Ins/VST3"
        AU_COPY_DIR   "/Library/Audio/Plug-Ins/Components")
endif()

juce_add_plugin(NECRONAM_MAX
    PRODUCT_NAME              "NECRONAM MAX"
    COMPANY_NAME             "CP Software"
    BUNDLE_ID                "com.cpsoftware.necronammax"
    DESCRIPTION              "Neural Amp Necromancy MAX — dual-amp NAM player, stereo dual-IR cab, EQ & saturation"
    PLUGIN_MANUFACTURER_CODE  Cpsw
    PLUGIN_CODE               NcMx
    FORMATS                   ${NMAX_FORMATS}
    IS_SYNTH                  FALSE
    NEEDS_MIDI_INPUT          FALSE
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    ${NMAX_COPY_ARGS}
    VST3_CATEGORIES           "Fx" "Distortion"
    AU_MAIN_TYPE              "kAudioUnitType_Effect"
)

juce_generate_juce_header(NECRONAM_MAX)

# Embedded assets (logo + body typefaces).
juce_add_binary_data(NecronamMaxAssets SOURCES
    Resources/Anton-Regular.ttf
    Resources/IBMPlexMono-Regular.ttf
    Resources/IBMPlexMono-Bold.ttf)

target_sources(NECRONAM_MAX PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/HorrorLookAndFeel.cpp
    ${NAM_CORE_SOURCES}
)

# NAM core processes float buffers (matches JUCE).
target_compile_definitions(NECRONAM_MAX PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    NAM_SAMPLE_FLOAT=1
)

# Include paths for NAM core + its vendored nlohmann/json and Eigen.
target_include_directories(NECRONAM_MAX PRIVATE
    "${NAM_DIR}"
    "${NAM_DEPS}"
    "${NAM_DEPS}/nlohmann"
)
target_include_directories(NECRONAM_MAX SYSTEM PRIVATE
    "${NAM_DEPS}/eigen"
)

if(EXISTS "${AAX_SDK_PATH}")
    target_link_libraries(NECRONAM_MAX PRIVATE AAXLibrary)
endif()

target_link_libraries(NECRONAM_MAX PRIVATE
    NecronamMaxAssets
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)
