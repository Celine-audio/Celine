#pragma once

#include <juce_data_structures/juce_data_structures.h>

//==============================================================================
/**
    Where the user keeps their .celsch files, and what is in there.

    One folder, remembered between runs, so presets are a menu rather than a
    trip through a file dialog every time. Deliberately *only* a folder and a
    listing: it opens nothing, writes no presets and has no opinion about what a
    preset contains -- the document format is PluginProcessor's business, and
    loading one still goes through restoreDocument() like every other route in.

    The folder lives in a settings file next to the host's own preferences,
    **not** in the plugin's state. A preset folder is a property of the machine
    someone is sitting at, not of the circuit they drew: saving it into the
    document would mean a sheet emailed to someone else quietly repointed their
    preset menu at a directory that doesn't exist on their computer.
*/
class PresetLibrary
{
public:
    /** Reads and writes the user's real settings file, next to the host's own
        preferences. */
    PresetLibrary();

    /** Points at a settings file of your choosing. For tests: the default
        constructor writes to the file the actual user's answers live in, so a
        test that used it would quietly mark the first-run question as already
        asked on the machine running the test. */
    explicit PresetLibrary (const juce::File& settingsFile);

    /** Sends every PresetLibrary built after this call -- including the ones
        this class doesn't build itself -- to a settings file of your choosing.

        This exists because a test harness constructs a real PluginEditor
        (`runWithinPluginEditor`, and the benchmarks do the same), and an editor
        asks the first-run question. Without the redirect, running the test suite
        answers that question on behalf of whoever is sitting at the machine, and
        they never get asked. It happened once; hence this.

        Call it from a test main, before anything else. */
    static void redirectSettingsForTesting (const juce::File& settingsFile);

    //==========================================================================
    /** True once a folder has been chosen and it still exists. A folder that
        has been deleted or unmounted since counts as no folder rather than as
        an empty one, so the menu offers to find a new one instead of claiming
        the presets are gone. */
    bool hasDirectory() const;

    juce::File getDirectory() const;
    void setDirectory (const juce::File& folder);

    /** Whether the user has been asked to pick a folder yet.
        Asked once, ever -- declining is an answer, and a dialog that reappears
        on every launch until it gets the answer it wants is not a question. */
    bool hasBeenOffered() const;
    void markOffered();

    //==========================================================================
    /** Every preset in the folder, ordered the way a person would order them.
        Empty when no folder is set, which is the same answer the menu wants for
        a folder that turned out to be missing.

        Not recursive: a flat folder is what the menu can show without becoming
        a tree, and a subfolder full of presets is a bank, which is a different
        feature to the one this is. */
    juce::Array<juce::File> getPresets() const;

    //==========================================================================
    /** The product name, without the version -- and the name of the folder the
        settings live in, which is why it is plain ASCII. Still decoded as UTF-8
        rather than through juce::String's byte-at-a-time conversion, so this
        stays correct if the name ever gains a character again. */
    static juce::String getProductName();

    /** A folder to put in front of the user when asking. Not created here --
        nothing should appear on anyone's disk before they have said yes. */
    static juce::File getSuggestedDirectory();

private:
    std::unique_ptr<juce::PropertiesFile> settings;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetLibrary)
};
