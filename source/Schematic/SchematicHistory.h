#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include <vector>

namespace SchematicModel
{
    //==========================================================================
    /** Undo and redo for the drawing, as whole-sheet snapshots (toValueTree()).

        Snapshots include knob and switch positions, but undo does not restore them: the
        editor takes those back from the parameters. */
    class SchematicHistory
    {
       public:
        /** Forgets the history and starts from `state`. For a preset load or session restore. */
        void reset(juce::ValueTree state)
        {
            undoStack.clear();
            redoStack.clear();
            current = std::move(state);
        }

        /** Records `now`, the state after an edit. The state before it is the one last
            recorded. */
        void record(juce::ValueTree now)
        {
            if (current.isValid())
                undoStack.push_back(current);

            current = std::move(now);

            // A new edit drops anything that was undone.
            redoStack.clear();

            while (undoStack.size() > maxDepth)
                undoStack.erase(undoStack.begin());
        }

        /** Replaces the newest state without adding a step, so a drag is one undo step. */
        void amend(juce::ValueTree now) { current = std::move(now); }

        bool canUndo() const noexcept { return ! undoStack.empty(); }
        bool canRedo() const noexcept { return ! redoStack.empty(); }

        /** The state to restore, or an invalid tree when there is nothing to undo. */
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
