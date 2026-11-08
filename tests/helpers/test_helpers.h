#pragma once
#include <PluginProcessor.h>

/** Runs a test with an editor attached to the processor, then tears both down. The
    editor is never on screen, so the first-run prompt doesn't fire. */
[[maybe_unused]] static void runWithinPluginEditor (const std::function<void (PluginProcessor& plugin)>& testCode)
{
    PluginProcessor plugin;
    const auto editor = plugin.createEditorAndMakeActive();

    testCode (plugin);

    plugin.editorBeingDeleted (editor);
    delete editor;
}
