// Every file in tests/ is globbed into this executable -- see cmake/Tests.cmake.

#include "PresetLibrary.h"
#include "juce_gui_basics/juce_gui_basics.h"
#include <ui/ThemePalette.h>
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

    // And off the real theme, for the same reason. The palette writes itself whenever a
    // colour changes, and the theming tests change every colour there is -- pointed at
    // the real file, running the suite would rewrite whatever palette the person at this
    // machine had chosen.
    const juce::File themeFile = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                     .getChildFile ("celine-tests-" + juce::Uuid().toString() + ".celthm");
    Celine::Theme::Palette::useFileForTesting (themeFile);

    const int result = Catch::Session().run (argc, argv);

    settings.deleteFile();

    themeFile.deleteFile();

    return result;
}
#include <catch2/catch_test_macros.hpp>
