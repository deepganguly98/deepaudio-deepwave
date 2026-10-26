cmake_minimum_required(VERSION 3.22)
project(WaveGrid VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# macOS:  cmake -B build -G Xcode
# iOS:    cmake -B build-ios -G Xcode -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0
# Windows: cmake -B build-win -A x64   (see windows/README_WINDOWS.md)
#          cmake --build build-win --config Release --target WaveGrid_Standalone WaveGrid_VST3
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(WG_FORMATS AUv3 Standalone)
    set(WG_COPY_AFTER_BUILD TRUE)
elseif(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
    set(WG_FORMATS AU AUv3 VST3 Standalone)
    set(WG_COPY_AFTER_BUILD TRUE)
else()
    # Windows / Linux. VST2 is not offered: Steinberg withdrew the VST2 SDK and JUCE 8 no longer supports it;
    # VST3 is what FL Studio (20.8+), Ableton, Reaper, Cubase, Bitwig etc. load today.
    set(WG_FORMATS VST3 Standalone)
    set(WG_COPY_AFTER_BUILD FALSE)   # copying into Program Files needs admin; the build script/installer does it
    if(MSVC)
        # self-contained: no Visual C++ redistributable needed on the user's PC
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        add_compile_options(/MP /utf-8)   # /utf-8: sources contain UTF-8 characters (e.g. in parameter names)
    endif()
endif()

# app icon: Windows wants a tightly cropped rounded square, macOS/iOS keep the system margin
if(WIN32)
    set(WG_ICON_BIG   "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon_win_1024.png")
    set(WG_ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon_win_256.png")
else()
    set(WG_ICON_BIG   "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon_mac_1024.png")
    set(WG_ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon_mac_256.png")
endif()

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(WaveGrid
    COMPANY_NAME             "DeepAudio"
    BUNDLE_ID                "com.deepaudio.deepwave"
    PLUGIN_MANUFACTURER_CODE Dpau
    PLUGIN_CODE              Wgrd
    FORMATS                  ${WG_FORMATS}
    PRODUCT_NAME             "DeepWave"
    IS_SYNTH                 TRUE
    NEEDS_MIDI_INPUT         TRUE
    NEEDS_MIDI_OUTPUT        TRUE
    IS_MIDI_EFFECT           FALSE
    AU_MAIN_TYPE             kAudioUnitType_MusicDevice
    VST3_CATEGORIES          Instrument Synth
    MICROPHONE_PERMISSION_ENABLED FALSE
    IPHONE_SCREEN_ORIENTATIONS UIInterfaceOrientationLandscapeLeft UIInterfaceOrientationLandscapeRight
    IPAD_SCREEN_ORIENTATIONS   UIInterfaceOrientationLandscapeLeft UIInterfaceOrientationLandscapeRight
    REQUIRES_FULL_SCREEN     TRUE
    ICON_BIG                 "${WG_ICON_BIG}"
    ICON_SMALL               "${WG_ICON_SMALL}"
    COPY_PLUGIN_AFTER_BUILD  ${WG_COPY_AFTER_BUILD})

target_sources(WaveGrid PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/ui/GridSurface.cpp)

target_include_directories(WaveGrid PRIVATE Source)

target_compile_definitions(WaveGrid PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(WaveGrid
    PRIVATE juce::juce_audio_utils juce::juce_dsp
    PUBLIC  juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

# ---- Tests (cmake -B build-test -DWAVEGRID_BUILD_TESTS=ON; cmake --build build-test --target WaveGrid_Tests)
option(WAVEGRID_BUILD_TESTS "Build the WaveGrid_Tests console app" OFF)
if(WAVEGRID_BUILD_TESTS AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    juce_add_console_app(WaveGrid_Tests PRODUCT_NAME "WaveGrid_Tests")
    target_sources(WaveGrid_Tests PRIVATE
        tests/Main.cpp
        tests/DspTests.cpp
        tests/ScaleTests.cpp
        tests/ArpTests.cpp
        tests/FxTests.cpp
        tests/PresetTests.cpp)
    target_include_directories(WaveGrid_Tests PRIVATE Source)
    target_compile_definitions(WaveGrid_Tests PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_UNIT_TESTS=1)
    target_link_libraries(WaveGrid_Tests
        PRIVATE juce::juce_dsp juce::juce_audio_basics
        PUBLIC  juce::juce_recommended_config_flags)
endif()

# ---- README screenshot tool (macOS): renders the real editor offscreen, no screen-recording permission needed
#   cmake -B build-shot -G Xcode -DWAVEGRID_BUILD_SCREENSHOT=ON && cmake --build build-shot --config Release --target WaveGrid_Screenshot
option(WAVEGRID_BUILD_SCREENSHOT "Build the offscreen screenshot tool" OFF)
if(WAVEGRID_BUILD_SCREENSHOT AND APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    juce_add_gui_app(WaveGrid_Screenshot PRODUCT_NAME "WaveGrid_Screenshot")
    target_sources(WaveGrid_Screenshot PRIVATE
        tools/screenshot/main.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/ui/GridSurface.cpp)
    target_include_directories(WaveGrid_Screenshot PRIVATE Source)
    target_compile_definitions(WaveGrid_Screenshot PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1 JucePlugin_Name="DeepWave")
    target_link_libraries(WaveGrid_Screenshot PRIVATE
        juce::juce_audio_utils juce::juce_audio_processors juce::juce_dsp
        juce::juce_recommended_config_flags)
endif()
