# Entry point used by Gradle (externalNativeBuild) -- see android/README_ANDROID.md.
# Can also be configured by hand with the NDK toolchain file.
cmake_minimum_required(VERSION 3.22)
project(WaveGridAndroid VERSION 0.1.0 LANGUAGES C CXX ASM)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Reuse an existing JUCE 8.0.4 checkout (needed anyway so Gradle can see JUCE's Java sources),
# otherwise download it.
set(WAVEGRID_JUCE_DIR "" CACHE PATH "Path to a JUCE 8.0.4 source tree")
if(WAVEGRID_JUCE_DIR)
    add_subdirectory("${WAVEGRID_JUCE_DIR}" juce EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/WaveGridAndroid.cmake")
