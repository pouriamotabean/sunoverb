cmake_minimum_required(VERSION 3.22)
project(SunoChain VERSION 2.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Use a local JUCE checkout if present (-DJUCE_DIR=...), otherwise download it.
if(DEFINED JUCE_DIR)
    add_subdirectory(${JUCE_DIR} juce)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

message(STATUS "Suno Chain ${PROJECT_VERSION} | JUCE 8.0.4 | ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION} | ${CMAKE_SYSTEM_NAME} ${CMAKE_SYSTEM_PROCESSOR}")

juce_add_plugin(SunoChain
    COMPANY_NAME "PM Audio"
    PLUGIN_MANUFACTURER_CODE Pmau
    PLUGIN_CODE Snch
    FORMATS VST3
    PRODUCT_NAME "Suno Chain"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

juce_generate_juce_header(SunoChain)

target_sources(SunoChain PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(SunoChain PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(SunoChain
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------------------------------------------
# Tests (v2.0.1): SunoChainTests runs the REAL processor (same sources as the plugin) offline; render_cli is the
# DSP-only renderer used by the Python tools. Both are built by CI and can be switched off with -DSUNOCHAIN_TESTS=OFF.
option(SUNOCHAIN_TESTS "Build SunoChainTests and render_cli" ON)
if(SUNOCHAIN_TESTS)
    juce_add_console_app(SunoChainTests PRODUCT_NAME "SunoChainTests")
    juce_generate_juce_header(SunoChainTests)
    target_sources(SunoChainTests PRIVATE
        tests/SunoChainTests.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp)
    target_include_directories(SunoChainTests PRIVATE Source)
    target_compile_definitions(SunoChainTests PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JucePlugin_Name="Suno Chain")
    target_link_libraries(SunoChainTests
        PRIVATE
            juce::juce_audio_processors
            juce::juce_gui_extra
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    add_executable(render_cli tools/render_cli.cpp)
    set_target_properties(render_cli PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    set_target_properties(SunoChainTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()
