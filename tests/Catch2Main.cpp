// Every file in tests/ is globbed into this executable -- see cmake/Tests.cmake.

#include "PresetLibrary.h"
#include "juce_gui_basics/juce_gui_basics.h"
#include <catch2/catch_session.hpp>

int main (int argc, char* argv[])
{
    // A MessageManager for the whole run, so anything touching juce::Graphics,
    // juce::Timer or the APVTS works without each test standing one up itself.
    juce::ScopedJuceInitialiser_GUI gui;

    // Keep the suite off the real user's preferences. runWithinPluginEditor
    // builds an actual PluginEditor, and an editor asks where presets should
    // live -- so without this, running the tests answers that question for
    // whoever owns the machine and they are never asked again.
    const auto settings = juce::File::getSpecialLocation (juce::File::tempDirectory)
                              .getChildFile ("celine-tests-" + juce::Uuid().toString() + ".settings");

    PresetLibrary::redirectSettingsForTesting (settings);

    const int result = Catch::Session().run (argc, argv);

    settings.deleteFile();

    return result;
}
#include <catch2/catch_test_macros.hpp>
