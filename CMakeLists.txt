cmake_minimum_required(VERSION 3.22)
project(QSStudio VERSION 0.8.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static C runtime on Windows: one self-contained .exe, nothing to install.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Core library: no JUCE, testable anywhere.
add_library(qscore STATIC core/qs_sysex.cpp core/qs_card.cpp core/qs_program.cpp core/qs_samples.cpp)
target_include_directories(qscore PUBLIC core)

add_executable(core_tests tests/core_tests.cpp)
target_link_libraries(core_tests PRIVATE qscore)
enable_testing()
add_test(NAME core_tests COMMAND core_tests)

option(QS_BUILD_APP "Build the JUCE desktop app" ON)
if(QS_BUILD_APP)
    include(FetchContent)
    FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.4 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)

    juce_add_gui_app(QSStudio
        PRODUCT_NAME "QS Studio"
        COMPANY_NAME "QS Studio"
        VERSION ${PROJECT_VERSION})
    juce_generate_juce_header(QSStudio)
    target_sources(QSStudio PRIVATE app/Main.cpp app/MainComponent.cpp app/LibraryTab.cpp app/EditorTab.cpp app/RecorderTab.cpp app/Pages.cpp)
    target_compile_definitions(QSStudio PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
        JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:QSStudio,JUCE_PRODUCT_NAME>"
        JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:QSStudio,JUCE_VERSION>")
    target_link_libraries(QSStudio PRIVATE qscore
        juce::juce_gui_extra juce::juce_audio_utils juce::juce_audio_devices juce::juce_audio_formats juce::juce_audio_basics juce::juce_cryptography
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()
