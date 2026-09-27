cmake_minimum_required(VERSION 3.22)
project(SoundNuts VERSION 2.0.0 LANGUAGES C CXX)

# foleys_gui_magic needs C++20; the JUCE-free DSP core stays C++17-compatible.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# --- JUCE (local; no global install): -DJUCE_DIR=... > external/JUCE ---------
if(DEFINED JUCE_DIR AND EXISTS "${JUCE_DIR}/CMakeLists.txt")
    add_subdirectory("${JUCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE")
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/JUCE/CMakeLists.txt")
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/external/JUCE" "${CMAKE_BINARY_DIR}/JUCE")
else()
    message(FATAL_ERROR "No local JUCE found — set -DJUCE_DIR=/path/to/JUCE")
endif()

# The WYSIWYG editor window opens next to the plugin. Turn it OFF for builds that
# go to other people (it also bakes this source path into the binary).
option(SOUNDNUTS_GUI_EDITOR "Open the foleys WYSIWYG editor window with the plugin" ON)
# OFF builds the bundles without copying them into ~/Library/Audio/Plug-Ins.
option(SOUNDNUTS_INSTALL_AFTER_BUILD "Copy the plugins into the user plug-in folders after building" ON)

# --- v2 identity (V2_CONVENTIONS.md): NEW code/bundle so v1 keeps coexisting -
juce_add_plugin(SoundNutsII
    PLUGIN_MANUFACTURER_CODE Sdnt
    PLUGIN_CODE              Snt2
    COMPANY_NAME             "SoundNuts"
    BUNDLE_ID                com.soundnuts.v2
    IS_SYNTH                 FALSE
    NEEDS_MIDI_INPUT         FALSE
    IS_MIDI_EFFECT           FALSE
    COPY_PLUGIN_AFTER_BUILD  ${SOUNDNUTS_INSTALL_AFTER_BUILD}
    PLUGIN_NAME              "SoundNuts II"
    PRODUCT_NAME             "SoundNuts II"
    FORMATS                  VST3 Standalone AU)

# --- default WYSIWYG layout baked in as BinaryData ---------------------------
juce_add_binary_data(SoundNutsAssets SOURCES Resources/magic.xml)

target_sources(SoundNutsII PRIVATE
    Source/PluginProcessor.cpp
    Source/ui/GuiItems.cpp)

# the JUCE-free, header-only DSP core lives one level up; acv2 = vendored
# shared v2 infra (Source/common/acv2)
target_include_directories(SoundNutsII PRIVATE
    Source
    Source/common
    "${CMAKE_CURRENT_SOURCE_DIR}/../dsp/include")

target_compile_definitions(SoundNutsII PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_REPORT_APP_USAGE=0)

# --- foleys_gui_magic (WYSIWYG GUI layer, module only — no example CMake) ----
if(DEFINED FOLEYS_DIR AND EXISTS "${FOLEYS_DIR}/modules/foleys_gui_magic/foleys_gui_magic.h")
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/foleys_gui_magic/modules/foleys_gui_magic/foleys_gui_magic.h")
    set(FOLEYS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/foleys_gui_magic")
else()
    message(FATAL_ERROR "No foleys_gui_magic checkout found — set -DFOLEYS_DIR=...")
endif()
juce_add_module("${FOLEYS_DIR}/modules/foleys_gui_magic")

target_compile_definitions(SoundNutsII PUBLIC
    FOLEYS_SHOW_GUI_EDITOR_PALLETTE=$<BOOL:${SOUNDNUTS_GUI_EDITOR}>
    FOLEYS_ENABLE_BINARY_DATA=0
    FOLEYS_ENABLE_OPEN_GL_CONTEXT=0)

target_link_libraries(SoundNutsII PRIVATE
    SoundNutsAssets
    foleys_gui_magic
    juce::juce_audio_utils juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# --- offline JUCE-free DSP checks, wired as CTest ----------------------------
enable_testing()

add_executable(soundnuts_check "${CMAKE_CURRENT_SOURCE_DIR}/../tools/soundnuts_check.cpp")
target_include_directories(soundnuts_check PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../dsp/include")
add_test(NAME soundnuts_core COMMAND soundnuts_check)

add_executable(soundnuts_v2_check "${CMAKE_CURRENT_SOURCE_DIR}/../tools/soundnuts_v2_check.cpp")
target_include_directories(soundnuts_v2_check PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../dsp/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/common")
add_test(NAME soundnuts_v2 COMMAND soundnuts_v2_check)

# --- offscreen UI screenshot tool (dev only; NOT a CTest) --------------------
# Links the juce_add_plugin SHARED-CODE lib so it gets the processor, the
# foleys editor and BinaryData without building/installing any bundle.
# Do NOT add juce::* or foleys_gui_magic here: those are INTERFACE libs that
# would recompile every JUCE module into this exe. SoundNutsII already carries
# them as $<LINK_ONLY:...>, which propagates the frameworks but not the sources.
add_executable(soundnuts_shot "${CMAKE_CURRENT_SOURCE_DIR}/tools/soundnuts_shot.cpp")
target_link_libraries(soundnuts_shot PRIVATE SoundNutsII)
target_include_directories(soundnuts_shot PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Source"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/common"
    "${CMAKE_CURRENT_SOURCE_DIR}/../dsp/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/external/JUCE/modules"
    "${FOLEYS_DIR}/modules")
target_compile_definitions(soundnuts_shot PRIVATE JucePlugin_Build_Standalone=1)
