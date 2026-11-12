#pragma once

#include "Schematic.h"

namespace SchematicModel
{
    //==========================================================================
    /** Starter circuits, laid out on the grid. */
    namespace Examples
    {
        /** Their names, for the menu. */
        juce::StringArray getNames();

        /** Replaces `schematic`'s contents with example `index`. */
        void load(Schematic& schematic, int index);

        /** The one a fresh instance opens with. */
        inline constexpr int defaultIndex = 0;
    } // namespace Examples
} // namespace SchematicModel
