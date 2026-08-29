#include "PresetLibrary.h"

#include "PluginProcessor.h"

namespace
{
    // Keys in the settings file. Strings rather than an enum because they end up
    // in a file someone may one day open in a text editor.
    const juce::String directoryKey { "presetDirectory" };
    const juce::String offeredKey { "presetDirectoryOffered" };

    /** Orders presets the way someone reading the menu would: case-insensitively
        and with runs of digits compared as numbers, so "Bass 10" comes after
        "Bass 9" rather than between "Bass 1" and "Bass 2". */
    struct PresetNameComparator
    {
        static int compareElements (const juce::File& a, const juce::File& b)
        {
            return a.getFileNameWithoutExtension().compareNatural (b.getFileNameWithoutExtension());
        }
    };
} // namespace

//==============================================================================
namespace
{
    /** Set by redirectSettingsForTesting, and otherwise never touched. A file
        rather than a flag, so there is exactly one thing to check and no way to
        be "in test mode" without somewhere to write. */
    juce::File& getSettingsOverride()
    {
        static juce::File override;
        return override;
    }

    juce::PropertiesFile::Options makeSettingsOptions()
    {
        juce::PropertiesFile::Options options;
        options.applicationName = PresetLibrary::getProductName();
        options.filenameSuffix = "settings";
        options.folderName = PresetLibrary::getProductName();
        options.osxLibrarySubFolder = "Application Support";

        return options;
    }
} // namespace

PresetLibrary::PresetLibrary()
    : settings (getSettingsOverride() != juce::File{}
                    ? std::make_unique<juce::PropertiesFile> (getSettingsOverride(), makeSettingsOptions())
                    : std::make_unique<juce::PropertiesFile> (makeSettingsOptions()))
{
}

void PresetLibrary::redirectSettingsForTesting (const juce::File& settingsFile)
{
    getSettingsOverride() = settingsFile;
}

PresetLibrary::PresetLibrary (const juce::File& settingsFile)
    : settings (std::make_unique<juce::PropertiesFile> (settingsFile, makeSettingsOptions()))
{
}

//==============================================================================
juce::File PresetLibrary::getDirectory() const
{
    const auto path = settings->getValue (directoryKey);
    return path.isEmpty() ? juce::File{} : juce::File (path);
}

bool PresetLibrary::hasDirectory() const
{
    const auto folder = getDirectory();
    return folder != juce::File{} && folder.isDirectory();
}

void PresetLibrary::setDirectory (const juce::File& folder)
{
    settings->setValue (directoryKey, folder.getFullPathName());

    // Written through immediately rather than on the timer. Another instance of
    // the plugin reads this file when its editor opens, and "I chose a folder
    // and the other window still doesn't know" is a confusing way to find out
    // there is a save delay.
    settings->saveIfNeeded();
}

//==============================================================================
bool PresetLibrary::hasBeenOffered() const
{
    return settings->getBoolValue (offeredKey, false);
}

void PresetLibrary::markOffered()
{
    settings->setValue (offeredKey, true);
    settings->saveIfNeeded();
}

//==============================================================================
juce::Array<juce::File> PresetLibrary::getPresets() const
{
    if (! hasDirectory())
        return {};

    // One wildcard, not a ";"-separated list: File::findChildFiles matches the
    // semicolon literally, unlike FileChooser, which splits on it.
    auto found = getDirectory().findChildFiles (juce::File::findFiles, false,
                                                juce::String ("*")
                                                    + PluginProcessor::circuitFileExtension);

    PresetNameComparator comparator;
    found.sort (comparator);

    return found;
}

//==============================================================================
juce::String PresetLibrary::getProductName()
{
    return juce::String::fromUTF8 (PRODUCT_NAME_WITHOUT_VERSION);
}

juce::File PresetLibrary::getSuggestedDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
        .getChildFile (getProductName() + " Presets");
}
