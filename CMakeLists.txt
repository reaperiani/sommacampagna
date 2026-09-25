cmake_minimum_required(VERSION 3.22)

project(SommaCampagnaVST3 VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set JUCE_DIR to your local JUCE checkout path, for example:
# cmake -B build -DJUCE_DIR="C:/dev/JUCE"
if(NOT DEFINED JUCE_DIR)
  message(FATAL_ERROR "JUCE_DIR is not set. Pass -DJUCE_DIR=<path-to-JUCE>")
endif()

add_subdirectory(${JUCE_DIR} JUCE)

set(PLUGIN_FORMATS VST3)
if(APPLE)
  list(APPEND PLUGIN_FORMATS AU)
elseif(UNIX AND NOT APPLE)
  list(APPEND PLUGIN_FORMATS LV2)
endif()

juce_add_plugin(sommacampagna
  COMPANY_NAME "reaperiani"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  COPY_PLUGIN_AFTER_BUILD FALSE
  PLUGIN_MANUFACTURER_CODE DShd
  PLUGIN_CODE HSmr
  FORMATS ${PLUGIN_FORMATS}
  PRODUCT_NAME "sommacampagna"
)

target_sources(sommacampagna
  PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/SummerDSP.h
)

target_compile_definitions(sommacampagna
  PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(sommacampagna
  PRIVATE
    juce::juce_audio_utils
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)
