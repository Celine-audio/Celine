#pragma once
#include <PluginProcessor.h>

/** Runs a test with a live editor attached to the processor, and tears both
    down afterwards.

    Note that the editor built here is never on screen, which some of the
    editor's own code checks for -- the first-run preset prompt deliberately
    waits for a window a person can actually see, so it does not fire in here.
*/
[[maybe_unused]] static void runWithinPluginEditor (const std::function<void (PluginProcessor& plugin)>& testCode)
{
    PluginProcessor plugin;
    const auto editor = plugin.createEditorAndMakeActive();

    testCode (plugin);

    plugin.editorBeingDeleted (editor);
    delete editor;
}
