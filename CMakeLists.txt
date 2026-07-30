cmake_minimum_required(VERSION 3.22...4.3)

if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING
        "macOS architectures to build")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING
        "oldest supported macOS version")
endif()

project(StackSampler VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)
include(FetchContent)

set(JUCE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(JUCE_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.15
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(StackSampler
    COMPANY_NAME "figtracer"
    COMPANY_WEBSITE "https://github.com/figtracer/stacksampler"
    COMPANY_COPYRIGHT "Copyright (c) 2026 Gustavo"
    BUNDLE_ID "com.figtracer.stacksampler"
    PLUGIN_MANUFACTURER_CODE Figt
    PLUGIN_CODE StSm
    PRODUCT_NAME "StackSampler"
    DESCRIPTION "minimal vst3 sample layering tool"
    FORMATS VST3 Standalone
    VST3_CATEGORIES Instrument Sampler
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
)

juce_generate_juce_header(StackSampler)

target_sources(StackSampler
    PRIVATE
        Source/LayerParameters.cpp
        Source/LayerParameters.h
        Source/PluginEditor.cpp
        Source/PluginEditor.h
        Source/PluginProcessor.cpp
        Source/PluginProcessor.h
        Source/StackSamplerCore.cpp
        Source/StackSamplerCore.h
)

target_include_directories(StackSampler
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Source
)

target_compile_definitions(StackSampler
    PUBLIC
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_WEB_BROWSER=0
)

target_link_libraries(StackSampler
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

if(BUILD_TESTING)
    juce_add_console_app(StackSamplerTests
        PRODUCT_NAME "StackSampler Tests"
    )

    juce_generate_juce_header(StackSamplerTests)

    target_sources(StackSamplerTests
        PRIVATE
            Source/LayerParameters.cpp
            Source/LayerParameters.h
            Source/PluginEditor.cpp
            Source/PluginEditor.h
            Source/PluginProcessor.cpp
            Source/PluginProcessor.h
            Source/StackSamplerCore.cpp
            Source/StackSamplerCore.h
            Tests/StackSamplerTests.cpp
    )

    target_include_directories(StackSamplerTests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/Source
    )

    target_compile_definitions(StackSamplerTests
        PRIVATE
            JUCE_UNIT_TESTS=1
            JUCE_USE_CURL=0
            JUCE_VST3_CAN_REPLACE_VST2=0
            JUCE_WEB_BROWSER=0
            JucePlugin_Name="StackSampler"
    )

    target_link_libraries(StackSamplerTests
        PRIVATE
            juce::juce_audio_utils
            juce::juce_dsp
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags
    )

    add_test(
        NAME StackSamplerTests
        COMMAND $<TARGET_FILE:StackSamplerTests>
    )
endif()
