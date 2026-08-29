// Every file in benchmarks/ is globbed into this executable -- see
// cmake/Benchmarks.cmake.

#include "PresetLibrary.h"
#include "juce_gui_basics/juce_gui_basics.h"
#include <catch2/catch_session.hpp>

int main (int argc, char* argv[])
{
    // A MessageManager for the whole run: the boot benchmarks stand up a real
    // editor, which needs one.
    juce::ScopedJuceInitialiser_GUI gui;

    // The benchmarks build a real editor too -- same reason as in the tests'
    // main: a benchmark run must not answer the user's first-run question.
    const auto settings = juce::File::getSpecialLocation (juce::File::tempDirectory)
                              .getChildFile ("celine-benchmarks-" + juce::Uuid().toString() + ".settings");

    PresetLibrary::redirectSettingsForTesting (settings);

    const int result = Catch::Session().run (argc, argv);

    settings.deleteFile();

    return result;
}

#include "PluginEditor.h"
#include "catch2/benchmark/catch_benchmark_all.hpp"
#include "catch2/catch_test_macros.hpp"

#include "Benchmarks.cpp"
