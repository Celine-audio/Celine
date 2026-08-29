cmake_minimum_required(VERSION 3.25)

# The shared CMake modules live in /cmake, which is a submodule.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include (PamplejuceVersion)
include (CPM)
include(PamplejuceMacOS)
include(JUCEDefaults)

# The internal name: the build target, the artefacts folder and the SharedCode
# library are all named after it, and nobody else ever sees it.
#
# It cannot have spaces, and it has to be plain ASCII -- CMake target names are
# limited to [A-Za-z0-9_.+-], so an accent here fails the configure outright
# with "The target name is reserved or not valid". The accent belongs in
# PRODUCT_NAME below, which is the name hosts display.
set(PROJECT_NAME "Celine")

set(PRODUCT_NAME "Celine")

set(COMPANY_NAME "Celine Audio")

# The macOS bundle identifier, and the CLAP id. ASCII only.
set(BUNDLE_ID "com.celineaudio.Celine")

# AU is only built on macOS -- JUCE skips it elsewhere -- so the same list works
# on every platform. LV2 is what Linux hosts (Ardour, Carla, Reaper) want.
set(FORMATS AU VST3 LV2 Standalone)

# Force JUCE to work with sccache
# instruct MSVC to embed debug info (/Z7) instead of emitting a .pdb
set(CMAKE_POLICY_DEFAULT_CMP0141        NEW      CACHE STRING "" FORCE)
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT Embedded CACHE STRING "" FORCE)

project(${PROJECT_NAME} VERSION ${CURRENT_VERSION})

# After project(), so the compiler is known.
include(PamplejuceLog)


# JUCE and the CLAP wrapper are submodules: `git submodule update --init
# --recursive` once after cloning. CI does it as part of actions/checkout.
add_subdirectory(JUCE)
add_subdirectory(modules/clap-juce-extensions EXCLUDE_FROM_ALL)

# See `docs/CMake API.md` in the JUCE repo for all config options
juce_add_plugin("${PROJECT_NAME}"
        # Icons for the standalone app
        ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/packaging/icon.png"

        COMPANY_NAME "${COMPANY_NAME}"
        BUNDLE_ID "${BUNDLE_ID}"

        # On MacOS, plugin is copied to /Users/yourname/Library/Audio/Plug-Ins/
        COPY_PLUGIN_AFTER_BUILD FALSE

        # The standalone build opens an audio *input*, and since macOS 10.14 an
        # app that does that without NSMicrophoneUsageDescription in its
        # Info.plist is handed a stream of silence -- no error, no prompt, just
        # a dead input. This is what puts the key there and makes macOS ask.
        # Hosted formats don't need it: the host owns the input device.
        MICROPHONE_PERMISSION_ENABLED TRUE
        MICROPHONE_PERMISSION_TEXT "Céline needs your audio input so it can run it through the circuit you have drawn."

        # Four characters each, first one upper case -- AU requires it.
        # Changing either makes hosts treat this as a different plugin.
        PLUGIN_MANUFACTURER_CODE Celi
        PLUGIN_CODE CEN1

        # LV2 identifies plugins by URI rather than by a four-character code, and
        # JUCE refuses to configure the LV2 target without one. It never has to
        # resolve; it just has to be unique and stable, because changing it makes
        # every host treat this as a different plugin.
        LV2URI "https://github.com/Celine-audio/Celine"

        FORMATS "${FORMATS}"

        PRODUCT_NAME "${PRODUCT_NAME}")

# Lets the plugin targets and the Tests target share our code without running
# into ODR violations.
add_library(SharedCode INTERFACE)

clap_juce_extensions_plugin(TARGET "${PROJECT_NAME}"
        CLAP_ID "${BUNDLE_ID}"
        CLAP_FEATURES audio-effect)

# Fast math, C++23 and a few other target defaults.
include(SharedCodeDefaults)

# Globbed with CONFIGURE_DEPENDS, so a new file in source/ is picked up without
# editing this list.
file(GLOB_RECURSE SourceFiles CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/source/*.h")
target_sources(SharedCode INTERFACE ${SourceFiles})

# Embeds everything in assets/ into the binary, but only if there is anything
# there: juce_add_binary_data refuses an empty source list, and nothing is
# embedded at the moment. Drop a file in assets/ and it comes back on its own.
file(GLOB_RECURSE AssetFiles CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/*")
list(FILTER AssetFiles EXCLUDE REGEX "/\\.DS_Store$")

if (AssetFiles)
    include(Assets)
endif ()

# MacOS only: Cleans up folder and target organization on Xcode.
include(XcodePrettify)

target_compile_definitions(SharedCode
        INTERFACE

        # Neither is used. Turning either on needs the matching NEEDS_* flag in
        # the juce_add_plugin call above.
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0

        # Only the Standalone windows build is affected.
        JUCE_ASIO=1

        CMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE}"
        VERSION="${CURRENT_VERSION}"

        # JucePlugin_Name carries the version, which is no good for naming a
        # settings folder -- it would move on every patch bump. This is the name
        # to show people and to name folders after.
        # ASCII for the same reason PRODUCT_NAME is: this one becomes the
        # settings folder, so it is a path too.
        PRODUCT_NAME_WITHOUT_VERSION="Celine"
)

# Modules link to SharedCode with INTERFACE visibility so that both the plugin
# targets and the Tests target get them.
target_link_libraries(SharedCode
        INTERFACE
        juce_audio_utils
        juce_audio_processors
        juce_dsp
        juce_gui_basics
        juce_gui_extra
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if (AssetFiles)
    target_link_libraries(SharedCode INTERFACE Assets)
endif ()

# Link the JUCE plugin targets our SharedCode target
target_link_libraries("${PROJECT_NAME}" PRIVATE SharedCode)

# IPP support, comment out to disable
include(PamplejuceIPP)

# Everything related to the tests target
include(Tests)

# A separate target for Benchmarks (keeps the Tests target fast)
include(Benchmarks)

# Output some config for CI (like our PRODUCT_NAME)
include(GitHubENV)
