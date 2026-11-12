/* What has to survive the window being closed and reopened (the editor is rebuilt
   each time). */
#include "helpers/test_helpers.h"

#include <PluginEditor.h>
#include <PresetLibrary.h>

#include <catch2/catch_test_macros.hpp>

TEST_CASE ("the loaded preset survives the editor", "[session]")
{
    PluginProcessor plugin;

    plugin.presetName = "Fuzz Face";
    plugin.presetIsFactory = true;

    // Opening and closing a window must not disturb it.
    auto* editor = plugin.createEditorAndMakeActive();
    plugin.editorBeingDeleted (editor);
    delete editor;

    CHECK (plugin.presetName == "Fuzz Face");
    CHECK (plugin.presetIsFactory);

    SECTION ("and a host session save/restore")
    {
        juce::MemoryBlock blob;
        plugin.getStateInformation (blob);

        PluginProcessor restored;
        restored.setStateInformation (blob.getData(), (int) blob.getSize());

        CHECK (restored.presetName == "Fuzz Face");
        CHECK (restored.presetIsFactory);
    }
}

TEST_CASE ("the editor's size survives the editor", "[session]")
{
    PluginProcessor plugin;

    auto* first = plugin.createEditorAndMakeActive();
    first->setSize (1400, 820);
    plugin.editorBeingDeleted (first);
    delete first;

    auto* second = plugin.createEditorAndMakeActive();
    CHECK (second->getWidth() == 1400);
    CHECK (second->getHeight() == 820);
    plugin.editorBeingDeleted (second);
    delete second;
}

TEST_CASE ("the preset folder is one answer across instances", "[session]")
{
    /* Every instance shares the settings file, so none may hold it open: it would read
       stale values and write them back on close. */
    const auto settings = juce::File::getSpecialLocation (juce::File::tempDirectory)
                              .getChildFile ("celine-session-test.settings");
    settings.deleteFile();

    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("celine-session-presets");
    folder.createDirectory();

    {
        PresetLibrary chooser (settings);
        PresetLibrary alreadyOpen (settings);   // as a second plugin instance would be

        chooser.setDirectory (folder);

        CHECK (alreadyOpen.getDirectory() == folder);

        PresetLibrary openedAfter (settings);
        CHECK (openedAfter.getDirectory() == folder);
    }

    // And nothing wrote a stale copy back on the way out.
    PresetLibrary nextRun (settings);
    CHECK (nextRun.getDirectory() == folder);

    settings.deleteFile();
    folder.deleteRecursively();
}
