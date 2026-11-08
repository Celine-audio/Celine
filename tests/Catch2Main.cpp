// Every file in tests/ is globbed into this executable (see cmake/Tests.cmake).

#include "PresetLibrary.h"
#include "juce_gui_basics/juce_gui_basics.h"
#include <CelineUI/ThemePalette.h>
#include <catch2/catch_session.hpp>

int main (int argc, char* argv[])
{
    // One MessageManager for the whole run.
    juce::ScopedJuceInitialiser_GUI gui;

    // Keep the suite off the real preset settings, or running the tests answers the
    // first-run question for the user.
    const auto settings = juce::File::getSpecialLocation (juce::File::tempDirectory)
                              .getChildFile ("celine-tests-" + juce::Uuid().toString() + ".settings");

    PresetLibrary::redirectSettingsForTesting (settings);

    // And off the real theme file, which the theming tests would overwrite.
    const juce::File themeFile = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                     .getChildFile ("celine-tests-" + juce::Uuid().toString() + ".celthm");
    Celine::Theme::Palette::useFileForTesting (themeFile);

    const int result = Catch::Session().run (argc, argv);

    settings.deleteFile();

    themeFile.deleteFile();

    return result;
}
#include <catch2/catch_test_macros.hpp>
