#include "PresetLibrary.h"

#include "PluginProcessor.h"
#include "ProductInfo.h"

namespace
{
    // Keys in the settings file.
    const juce::String directoryKey { "presetDirectory" };
    const juce::String offeredKey { "presetDirectoryOffered" };

    /** Natural order, case-insensitive: "Bass 9" before "Bass 10". */
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
    /** Set by redirectSettingsForTesting. */
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

        // The company folder, shared by all the house plugins.
        options.folderName = juce::String (juce::CharPointer_UTF8 (ProductInfo::companyName));
        options.osxLibrarySubFolder = "Application Support";

        return options;
    }

    /** The old location, under the plugin's own name. Read once to migrate. */
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

juce::PropertiesFile::Options PresetLibrary::getSettingsOptions()
{
    return makeSettingsOptions();
}

PresetLibrary::PresetLibrary (const juce::File& file)
    : settingsFile (file)
{
}

//==============================================================================
/* Opened for each read and write, never held open: every instance shares the file,
   and a held copy would go stale. */
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

    // Saved immediately, so other instances see the change.
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

    // One wildcard: findChildFiles does not split on ";".
    auto found = getDirectory().findChildFiles (juce::File::findFiles, false,
                                                juce::String ("*")
                                                    + PluginProcessor::circuitFileExtension);

    found.removeIf ([] (const juce::File& file) { return isHidden (file); });

    PresetNameComparator comparator;
    found.sort (comparator);

    return found;
}

bool PresetLibrary::isHidden (const juce::File& file)
{
    return file.isHidden() || file.getFileName().startsWithChar ('.');
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
