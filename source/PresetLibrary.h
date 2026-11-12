#pragma once

#include <juce_data_structures/juce_data_structures.h>

//==============================================================================
/** The user's preset folder and the .celsch files in it.

    The folder is kept in a per-machine settings file, not in the plugin state, so a
    shared sheet never changes someone else's preset folder. */
class PresetLibrary
{
public:
    /** Uses the real settings file, next to the host's preferences. */
    PresetLibrary();

    /** Uses the given settings file. For tests. */
    explicit PresetLibrary (const juce::File& settingsFile);

    /** Makes every PresetLibrary built after this use `settingsFile`. Call it from a test
        main, before anything else, so tests never touch the user's real settings. */
    static void redirectSettingsForTesting (const juce::File& settingsFile);

    /** The settings file in use. Also holds UpdateCheck's preferences. */
    const juce::File& getSettingsFile() const noexcept { return settingsFile; }

    /** How the settings file is opened. */
    static juce::PropertiesFile::Options getSettingsOptions();

    //==========================================================================
    /** True when a folder is set and still exists. */
    bool hasDirectory() const;

    juce::File getDirectory() const;
    void setDirectory (const juce::File& folder);

    /** Whether the user has been asked to pick a folder yet. Asked only once. */
    bool hasBeenOffered() const;
    void markOffered();

    //==========================================================================
    /** The presets in the folder, in natural order. Not recursive. Empty when no folder
        is set. */
    juce::Array<juce::File> getPresets() const;

    //==========================================================================
    /** The product name without the version; also the settings folder's name. */
    static juce::String getProductName();

    /** Whether the library leaves a file or folder out: hidden as the platform says, or
        named with a leading dot. macOS and Linux hide dot-names and Windows does not (it
        hides by attribute), so without the second a folder synced from a Mac listed its
        ".something" files on Windows only. */
    static bool isHidden (const juce::File& file);

    /** A folder to suggest when asking. Not created here. */
    static juce::File getSuggestedDirectory();

private:
    /** Path to the settings file. Opened fresh for each read and write. */
    juce::File settingsFile;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetLibrary)
};
