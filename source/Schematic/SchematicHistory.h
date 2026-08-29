#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include <vector>

namespace SchematicModel
{
    //==========================================================================
    /**
        Undo and redo for the drawing, as whole-sheet snapshots.

        Snapshots rather than a stack of `UndoableAction`s: `toValueTree` is
        already exercised on every save and every host session, so a snapshot is
        one call that cannot disagree with the format, where an action class per
        mutation site is a fresh chance to restore something *almost* right.
        Sheets are a few kilobytes.

        A snapshot carries every pot position and switch throw, but does not
        *restore* them -- the editor seeds those back off the parameters after
        every undo. Those live in host-automatable parameters, and rewinding one
        behind an automation lane's back is fighting the host for control of it.
    */
    class SchematicHistory
    {
       public:
        /** Forgets everything and takes `state` as the new starting point. For a
            preset load or a host restoring a session -- undoing across one of
            those would put back a drawing from a different document. */
        void reset(juce::ValueTree state)
        {
            undoStack.clear();
            redoStack.clear();
            current = std::move(state);
        }

        /** Records that the sheet has moved on from what it was.

            `now` is the state *after* the edit; the state before it is whatever
            was last handed in. That is what lets this sit on a plain "something
            changed" notification rather than needing a hook before every
            mutation -- which is the one thing the canvas cannot reliably
            provide, since a drag mutates continuously. */
        void record(juce::ValueTree now)
        {
            if (current.isValid())
                undoStack.push_back(current);

            current = std::move(now);

            // A new edit is a new branch: whatever was undone is no longer
            // reachable, and offering redo after it would put back a change that
            // belongs to a history that no longer exists.
            redoStack.clear();

            // Bounded, because a long session's worth of a busy sheet is real
            // memory and nobody undoes sixty-four steps.
            while (undoStack.size() > maxDepth)
                undoStack.erase(undoStack.begin());
        }

        /** Replaces the newest recorded state without pushing a new one, so a
            continuous gesture is one undo step rather than one per grid square. */
        void amend(juce::ValueTree now) { current = std::move(now); }

        bool canUndo() const noexcept { return ! undoStack.empty(); }
        bool canRedo() const noexcept { return ! redoStack.empty(); }

        /** The state to restore, or an invalid tree when there is nothing to
            undo. */
        juce::ValueTree undo()
        {
            if (! canUndo())
                return {};

            redoStack.push_back(current);
            current = undoStack.back();
            undoStack.pop_back();

            return current;
        }

        juce::ValueTree redo()
        {
            if (! canRedo())
                return {};

            undoStack.push_back(current);
            current = redoStack.back();
            redoStack.pop_back();

            return current;
        }

        static constexpr size_t maxDepth = 64;

       private:
        std::vector<juce::ValueTree> undoStack, redoStack;
        juce::ValueTree current;
    };
} // namespace SchematicModel
