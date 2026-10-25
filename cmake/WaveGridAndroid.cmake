# WaveGridAndroid.cmake
# Defines the Android native library for WaveGrid. Include it AFTER JUCE has been made available
# (FetchContent_MakeAvailable / add_subdirectory). It does nothing on other platforms.
#
# What this produces: libjuce_jni.so (JUCE's Java side hard-codes System.loadLibrary("juce_jni")).
# JUCE 8.0.4's CMake API has no APK/Gradle generation ("Android targets are not currently supported"
# in docs/CMake API.md), so the .so is packaged into an APK by the Gradle project in android/.

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Android")
    return()
endif()

get_filename_component(WG_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

juce_add_gui_app(WaveGridAndroid
    COMPANY_NAME     "DeepAudio"
    BUNDLE_ID        "com.deepaudio.wavegrid"
    PRODUCT_NAME     "WaveGrid"
    VERSION          "0.1.0")
# Note: ICON_BIG is not used on Android (JUCEUtils.cmake only converts icons for macOS/Windows/iOS).
# The launcher icon comes from android/res via the Gradle project (mipmap/ic_launcher).

# JUCE's Android Java code loads "juce_jni"; _juce_set_output_name() is a no-op on Android.
set_target_properties(WaveGridAndroid PROPERTIES OUTPUT_NAME juce_jni)

target_sources(WaveGridAndroid PRIVATE
    ${WG_ROOT}/Source/android/AndroidMain.cpp
    ${WG_ROOT}/Source/PluginProcessor.cpp
    ${WG_ROOT}/Source/PluginEditor.cpp
    ${WG_ROOT}/Source/ui/GridSurface.cpp)

target_include_directories(WaveGridAndroid PRIVATE ${WG_ROOT}/Source)

target_compile_definitions(WaveGridAndroid PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_MODAL_LOOPS_PERMITTED=0
    JUCE_USE_ANDROID_OBOE=1
    # PluginProcessor.h uses JucePlugin_Name, normally supplied by juce_add_plugin.
    JucePlugin_Name="WaveGrid")

target_link_libraries(WaveGridAndroid
    PRIVATE juce::juce_audio_utils
            juce::juce_audio_devices
            juce::juce_dsp
            juce::juce_gui_basics
    PUBLIC  juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
