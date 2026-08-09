cmake_minimum_required(VERSION 3.22)
project(ArborBody VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.6
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(ArborBody
    COMPANY_NAME              PerceptionLab
    PLUGIN_MANUFACTURER_CODE  Pcpt
    PLUGIN_CODE               Arbd
    IS_SYNTH                  TRUE
    NEEDS_MIDI_INPUT          TRUE
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD   TRUE
    FORMATS                   VST3 Standalone AU
    PRODUCT_NAME              "ArborBody")

target_sources(ArborBody PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Core/ArborGraph.cpp
    Core/Mesh.cpp
    Core/Voice.cpp
    Core/Instrument.cpp)

target_compile_definitions(ArborBody PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(ArborBody PRIVATE
    juce::juce_audio_utils
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# ---- the JUCE-free core tests and demo renderer -----------------------------
add_executable(arborbody_tests
    Tests/core_test.cpp Core/ArborGraph.cpp Core/Mesh.cpp Core/Voice.cpp Core/Instrument.cpp)
add_executable(arborbody_demo
    Tests/render_demo.cpp Core/ArborGraph.cpp Core/Mesh.cpp Core/Voice.cpp Core/Instrument.cpp)
enable_testing()
add_test(NAME core COMMAND arborbody_tests)
