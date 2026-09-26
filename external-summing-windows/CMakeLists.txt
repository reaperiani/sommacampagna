cmake_minimum_required(VERSION 3.22)

if(APPLE)
  set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures")
  set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")
endif()

project(SommacampagnaExternalSumming VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT DEFINED JUCE_DIR)
  message(FATAL_ERROR "JUCE_DIR is not set. Pass -DJUCE_DIR=<path-to-JUCE>")
endif()

add_subdirectory(${JUCE_DIR} JUCE)

juce_add_gui_app(sommacampagna_engine
  COMPANY_NAME "reaperiani"
  COMPANY_WEBSITE "https://github.com/reaperiani/sommacampagna"
  BUNDLE_ID "com.reaperiani.sommacampagna.engine"
  PRODUCT_NAME "sommacampagna_engine"
)

target_sources(sommacampagna_engine PRIVATE
  engine/main.cpp
)

target_include_directories(sommacampagna_engine PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/shared
  ${CMAKE_CURRENT_SOURCE_DIR}/../Source
)

if(WIN32)
  target_link_libraries(sommacampagna_engine PRIVATE ws2_32)
endif()

target_link_libraries(sommacampagna_engine PRIVATE
  juce::juce_gui_extra
  juce::juce_audio_basics
)

target_compile_definitions(sommacampagna_engine PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  SOMMACAMPAGNA_VERSION="${PROJECT_VERSION}"
)

juce_add_plugin(sommacampagna_sender
  COMPANY_NAME "reaperiani"
  COMPANY_WEBSITE "https://github.com/reaperiani/sommacampagna"
  BUNDLE_ID "com.reaperiani.sommacampagna.sender"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  COPY_PLUGIN_AFTER_BUILD FALSE
  PLUGIN_MANUFACTURER_CODE DShd
  PLUGIN_CODE HSSd
  FORMATS VST3
  PRODUCT_NAME "sommacampagna_sender"
)

target_sources(sommacampagna_sender PRIVATE
  sender/PluginProcessor.h
  sender/PluginProcessor.cpp
  sender/PluginEditor.h
  sender/PluginEditor.cpp
)

target_include_directories(sommacampagna_sender PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/shared
)

target_compile_definitions(sommacampagna_sender PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(sommacampagna_sender PRIVATE
  juce::juce_audio_utils
)

juce_add_plugin(sommacampagna_receiver
  COMPANY_NAME "reaperiani"
  COMPANY_WEBSITE "https://github.com/reaperiani/sommacampagna"
  BUNDLE_ID "com.reaperiani.sommacampagna.receiver"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  COPY_PLUGIN_AFTER_BUILD FALSE
  PLUGIN_MANUFACTURER_CODE DShd
  PLUGIN_CODE HSRc
  FORMATS VST3
  PRODUCT_NAME "sommacampagna_receiver"
)

target_sources(sommacampagna_receiver PRIVATE
  receiver/PluginProcessor.h
  receiver/PluginProcessor.cpp
  receiver/PluginEditor.h
  receiver/PluginEditor.cpp
)

target_include_directories(sommacampagna_receiver PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/shared
)

target_compile_definitions(sommacampagna_receiver PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(sommacampagna_receiver PRIVATE
  juce::juce_audio_utils
)

if(APPLE)
  juce_add_plugin(sommacampagna_sender_au
    COMPANY_NAME "reaperiani"
    COMPANY_WEBSITE "https://github.com/reaperiani/sommacampagna"
    BUNDLE_ID "com.reaperiani.sommacampagna.sender.au"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE Reap
    PLUGIN_CODE Ssnd
    FORMATS AU
    PRODUCT_NAME "sommacampagna_sender"
  )

  target_sources(sommacampagna_sender_au PRIVATE
    sender/PluginProcessor.h
    sender/PluginProcessor.cpp
    sender/PluginEditor.h
    sender/PluginEditor.cpp
  )

  target_include_directories(sommacampagna_sender_au PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/shared
  )

  target_compile_definitions(sommacampagna_sender_au PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
  )

  target_link_libraries(sommacampagna_sender_au PRIVATE
    juce::juce_audio_utils
  )

  juce_add_plugin(sommacampagna_receiver_au
    COMPANY_NAME "reaperiani"
    COMPANY_WEBSITE "https://github.com/reaperiani/sommacampagna"
    BUNDLE_ID "com.reaperiani.sommacampagna.receiver.au"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE Reap
    PLUGIN_CODE Rrcv
    FORMATS AU
    PRODUCT_NAME "sommacampagna_receiver"
  )

  target_sources(sommacampagna_receiver_au PRIVATE
    receiver/PluginProcessor.h
    receiver/PluginProcessor.cpp
    receiver/PluginEditor.h
    receiver/PluginEditor.cpp
  )

  target_include_directories(sommacampagna_receiver_au PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/shared
  )

  target_compile_definitions(sommacampagna_receiver_au PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
  )

  target_link_libraries(sommacampagna_receiver_au PRIVATE
    juce::juce_audio_utils
  )
endif()
