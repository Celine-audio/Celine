#include "PresetLibrary.h"

#include "PluginProcessor.h"
#include "ProductInfo.h"

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

        // The company folder, not this plugin's own: it is where the rest of the house
        // keeps the choices that belong to the person rather than to a session, and one
        // folder per plugin is four places to look when something is wrong.
        options.folderName = juce::String (juce::CharPointer_UTF8 (ProductInfo::companyName));
        options.osxLibrarySubFolder = "Application Support";

        return options;
    }

    /** Where this used to be kept, under the plugin's own name. Read once, if the new
        one is not there yet, so upgrading does not silently forget a folder somebody
        chose -- which would look exactly like the bug this move was part of fixing. */
    juce::File legacySettingsFile()
    {
        juce::PropertiesFile::Options options = makeSettingsOptions();
        options.folderName = PresetLibrary::getProductName();

        return options.getDefaultFile();
    }

    juce::File defaultSettingsFile()
    {
        const auto file = makeSettingsOptions().getDefaultFile();

        if (! file.existsAsFile())
            if (const auto legacy = legacySettingsFile(); legacy.existsAsFile())
            {
                file.getParentDirectory().createDirectory();
                legacy.copyFileTo (file);
            }

        return file;
    }
} // namespace

PresetLibrary::PresetLibrary()
    : settingsFile (getSettingsOverride() != juce::File{} ? getSettingsOverride()
                                                          : defaultSettingsFile())
{
}

void PresetLibrary::redirectSettingsForTesting (const juce::File& settingsFile)
{
    getSettingsOverride() = settingsFile;
}

PresetLibrary::PresetLibrary (const juce::File& file)
    : settingsFile (file)
{
}

//==============================================================================
/*
    Opened for each read and write rather than held open for this object's lifetime.

    Every instance of the plugin shares this file, and one that held it open would be
    answering from the copy it read when it was created -- so a folder chosen in the
    window you have open now would be invisible to the instance loaded five minutes
    ago, and that instance would write its stale copy back over yours when it closed.
    The cost is a small file parsed per call, on the message thread, when a menu opens.
*/
juce::File PresetLibrary::getDirectory() const
{
    const juce::PropertiesFile file (settingsFile, makeSettingsOptions());
    const auto path = file.getValue (directoryKey);

    return path.isEmpty() ? juce::File{} : juce::File (path);
}

bool PresetLibrary::hasDirectory() const
{
    const auto folder = getDirectory();
    return folder != juce::File{} && folder.isDirectory();
}

void PresetLibrary::setDirectory (const juce::File& folder)
{
    juce::PropertiesFile file (settingsFile, makeSettingsOptions());
    file.setValue (directoryKey, folder.getFullPathName());

    // Written through immediately rather than on the timer. Another instance of
    // the plugin reads this file when its editor opens, and "I chose a folder
    // and the other window still doesn't know" is a confusing way to find out
    // there is a save delay.
    file.saveIfNeeded();
}

//==============================================================================
bool PresetLibrary::hasBeenOffered() const
{
    const juce::PropertiesFile file (settingsFile, makeSettingsOptions());
    return file.getBoolValue (offeredKey, false);
}

void PresetLibrary::markOffered()
{
    juce::PropertiesFile file (settingsFile, makeSettingsOptions());
    file.setValue (offeredKey, true);
    file.saveIfNeeded();
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
