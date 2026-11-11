cmake_minimum_required(VERSION 3.22)

set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "Apple silicon only")
set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum macOS")

project(SF2Player VERSION 1.0.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)

add_subdirectory(ThirdParty/JUCE)

juce_add_plugin(SF2Player
    PRODUCT_NAME "Okami SF2 Player"
    COMPANY_NAME "Marguento"
    BUNDLE_ID "com.marguento.SF2Player"
    PLUGIN_MANUFACTURER_CODE Mrgt
    PLUGIN_CODE Sf2p
    FORMATS AU VST3 Standalone
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    VST3_CATEGORIES Instrument Sampler
    COPY_PLUGIN_AFTER_BUILD TRUE)

target_sources(SF2Player PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

# FluidSynth, built once as a static library (see README).
set(FLUIDSYNTH_DIR ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/fluidsynth-install)
target_include_directories(SF2Player PRIVATE ${FLUIDSYNTH_DIR}/include)
target_link_libraries(SF2Player PRIVATE ${FLUIDSYNTH_DIR}/lib/libfluidsynth.a)

target_compile_definitions(SF2Player PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(SF2Player
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Offline check: tests/RenderTest plays a note through the processor and prints the level.
juce_add_console_app(RenderTest PRODUCT_NAME "RenderTest")
target_sources(RenderTest PRIVATE Tests/RenderTest.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_include_directories(RenderTest PRIVATE ${FLUIDSYNTH_DIR}/include)
target_link_libraries(RenderTest PRIVATE ${FLUIDSYNTH_DIR}/lib/libfluidsynth.a)
target_compile_definitions(RenderTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JucePlugin_Name="Okami SF2 Player")
target_link_libraries(RenderTest PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)

juce_add_console_app(TailTest PRODUCT_NAME "TailTest")
target_sources(TailTest PRIVATE Tests/TailTest.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_include_directories(TailTest PRIVATE ${FLUIDSYNTH_DIR}/include)
target_compile_definitions(TailTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JucePlugin_Name="Okami SF2 Player")
target_link_libraries(TailTest PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags ${FLUIDSYNTH_DIR}/lib/libfluidsynth.a)
