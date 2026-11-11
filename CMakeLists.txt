cmake_minimum_required(VERSION 3.22)

# Native for everyday builds; scripts/release.sh builds universal (arm64;x86_64).
set(CMAKE_OSX_ARCHITECTURES "${CMAKE_HOST_SYSTEM_PROCESSOR}" CACHE STRING "arm64, x86_64, or both for a release")
set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum macOS")

project(SF2Player VERSION 1.2.1 LANGUAGES C CXX)
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
# libvorbisfile, libvorbis and libogg (ThirdParty/xiph-install): FluidSynth decodes SF3 with them.
set(XIPH_DIR ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/xiph-install)
set(FLUID_LIBS ${FLUIDSYNTH_DIR}/lib/libfluidsynth.a ${XIPH_DIR}/lib/libvorbisfile.a ${XIPH_DIR}/lib/libvorbis.a ${XIPH_DIR}/lib/libogg.a)
target_include_directories(SF2Player PRIVATE ${FLUIDSYNTH_DIR}/include)
target_link_libraries(SF2Player PRIVATE ${FLUID_LIBS})

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
target_link_libraries(RenderTest PRIVATE ${FLUID_LIBS})
target_compile_definitions(RenderTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JucePlugin_Name="Okami SF2 Player" JucePlugin_VersionString="${PROJECT_VERSION}")
target_link_libraries(RenderTest PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)

juce_add_console_app(TailTest PRODUCT_NAME "TailTest")
target_sources(TailTest PRIVATE Tests/TailTest.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_include_directories(TailTest PRIVATE ${FLUIDSYNTH_DIR}/include)
target_compile_definitions(TailTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JucePlugin_Name="Okami SF2 Player" JucePlugin_VersionString="${PROJECT_VERSION}")
target_link_libraries(TailTest PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags ${FLUID_LIBS})

# The test suite: `cmake --build build && ctest --test-dir build --output-on-failure`.
juce_add_console_app(PluginTests PRODUCT_NAME "PluginTests")
target_sources(PluginTests PRIVATE Tests/PluginTests.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_include_directories(PluginTests PRIVATE ${FLUIDSYNTH_DIR}/include)
target_compile_definitions(PluginTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JucePlugin_Name="Okami SF2 Player" JucePlugin_VersionString="${PROJECT_VERSION}")
target_link_libraries(PluginTests PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags ${FLUID_LIBS})
enable_testing()
add_test(NAME PluginTests COMMAND PluginTests)
