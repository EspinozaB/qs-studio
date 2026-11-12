cmake_minimum_required(VERSION 3.22)
project(QSStudio VERSION 0.6.2 LANGUAGES C CXX)
# Release stage shown with the version: pre-alpha, alpha, beta, rc.1 ... and "" for a final release. See README "Versions".
set(QS_STAGE "pre-alpha")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static C runtime on Windows: one self-contained .exe, nothing to install.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Core library: no JUCE, testable anywhere.
add_library(qscore STATIC core/qs_sysex.cpp core/qs_card.cpp core/qs_program.cpp core/qs_samples.cpp core/qs_factory.cpp core/qs_mix.cpp core/qs_effects.cpp)
target_include_directories(qscore PUBLIC core)
set_target_properties(qscore PROPERTIES POSITION_INDEPENDENT_CODE ON)  # also linked into the plugin

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
        BUNDLE_ID "io.github.espinozab.qsstudio"
        ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.png"
        VERSION ${PROJECT_VERSION})
    juce_generate_juce_header(QSStudio)
    target_sources(QSStudio PRIVATE app/Main.cpp app/MainComponent.cpp app/LibraryTab.cpp app/EditorTab.cpp app/RecorderTab.cpp app/Pages.cpp app/MixTab.cpp app/EffectsTab.cpp app/PlayerTab.cpp)
    target_compile_definitions(QSStudio PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
        JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:QSStudio,JUCE_PRODUCT_NAME>"
        JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:QSStudio,JUCE_VERSION>"
        QS_STAGE="${QS_STAGE}")
    target_link_libraries(QSStudio PRIVATE qscore
        juce::juce_gui_extra juce::juce_audio_utils juce::juce_audio_devices juce::juce_audio_formats juce::juce_audio_basics juce::juce_cryptography
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()

# QS Sound Player plugin: the app's Sound Player as VST3 (and AU on macOS, LV2 on Linux) plus a standalone build.
option(QS_BUILD_PLUGIN "Build the QS Sound Player plugin" ON)
if(QS_BUILD_APP AND QS_BUILD_PLUGIN)
    set(QS_PLUGIN_FORMATS VST3 Standalone)
    if(APPLE)
        list(APPEND QS_PLUGIN_FORMATS AU)
    elseif(UNIX)
        list(APPEND QS_PLUGIN_FORMATS LV2)
    endif()
    juce_add_plugin(QSSoundPlayer
        PRODUCT_NAME "QS Sound Player"
        COMPANY_NAME "QS Studio"
        BUNDLE_ID "io.github.espinozab.qssoundplayer"
        PLUGIN_MANUFACTURER_CODE Qsst
        PLUGIN_CODE Qssp
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        LV2URI "https://github.com/EspinozaB/qs-studio/sound-player"
        VST3_CATEGORIES Instrument Sampler
        AU_MAIN_TYPE kAudioUnitType_MusicDevice
        ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.png"
        FORMATS ${QS_PLUGIN_FORMATS}
        VERSION ${PROJECT_VERSION})
    juce_generate_juce_header(QSSoundPlayer)
    target_sources(QSSoundPlayer PRIVATE plugin/PluginProcessor.cpp)
    target_compile_definitions(QSSoundPlayer PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 QS_STAGE="${QS_STAGE}")
    target_link_libraries(QSSoundPlayer PRIVATE qscore
        juce::juce_audio_utils juce::juce_audio_processors juce::juce_audio_basics
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()
