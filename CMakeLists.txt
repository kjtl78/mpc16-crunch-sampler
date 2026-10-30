cmake_minimum_required(VERSION 3.22)

project(MPC16Sampler VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Allow pointing to a local JUCE installation via -DJUCE_DIR=/path/to/JUCE
# Otherwise automatically fetch JUCE 8.0.4 via FetchContent
if(DEFINED JUCE_DIR AND EXISTS "${JUCE_DIR}/CMakeLists.txt")
    message(STATUS "Using local JUCE from ${JUCE_DIR}")
    add_subdirectory("${JUCE_DIR}" JUCE EXCLUDE_FROM_ALL)
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    message(STATUS "Using workspace JUCE from ${CMAKE_CURRENT_SOURCE_DIR}/JUCE")
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/JUCE" JUCE EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    message(STATUS "Fetching JUCE via FetchContent...")
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.4
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(MPC16Sampler
    COMPANY_NAME                "AntigravityAudio"
    PLUGIN_MANUFACTURER_CODE    AgAu
    PLUGIN_CODE                 Mpc1
    FORMATS                     VST3 AU Standalone
    PRODUCT_NAME                "MPC-16 Crunch Sampler"
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_AUTO_MANIFEST          FALSE
)

juce_generate_juce_header(MPC16Sampler)

target_sources(MPC16Sampler
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
)

target_include_directories(MPC16Sampler
    PRIVATE
        Source
)

target_compile_definitions(MPC16Sampler
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(MPC16Sampler
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
