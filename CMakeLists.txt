cmake_minimum_required(VERSION 3.22)
project(IRCapture VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        develop
    GIT_SHALLOW    FALSE
)
FetchContent_MakeAvailable(JUCE)

set(InfoPlistContent "<?xml version=\"1.0\" encoding=\"UTF-8\"?>
<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">
<plist version=\"1.0\">
<dict>
    <key>NSMicrophoneUsageDescription</key>
    <string>IR Capture needs microphone access to record impulse responses.</string>
</dict>
</plist>")

set(PLIST_FILE "${CMAKE_CURRENT_BINARY_DIR}/ExtraInfo.plist")
file(WRITE "${PLIST_FILE}" "${InfoPlistContent}")

juce_add_gui_app(IRCapture
    PRODUCT_NAME        "IR Capture"
    VERSION             "1.0.0"
    BUNDLE_ID           "com.voidcraftaudio.ircapture"
    PLIST_TO_MERGE      "${PLIST_FILE}"
)

juce_generate_juce_header(IRCapture)

target_sources(IRCapture
    PRIVATE
        Source/Main.cpp
        Source/MainComponent.cpp
        Source/LevelMeter.cpp
        Source/IrCapture.cpp
)

target_compile_definitions(IRCapture
    PRIVATE
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:IRCapture,JUCE_PRODUCT_NAME>"
        JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:IRCapture,JUCE_VERSION>"
)

target_link_libraries(IRCapture
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_devices
        juce::juce_dsp
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
