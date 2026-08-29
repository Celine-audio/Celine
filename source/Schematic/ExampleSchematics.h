#pragma once

#include "Schematic.h"

namespace SchematicModel
{
    //==========================================================================
    /**
        Circuits to start from, laid out on the grid.

        These exist so the plugin opens into something that makes a sound, and so
        there is a worked example of each kind of part to copy from rather than a
        blank sheet and a palette. They are meant to be pulled apart.
    */
    namespace Examples
    {
        /** How many there are, and their names for a menu. */
        juce::StringArray getNames();

        /** Replaces `schematic`'s contents with example `index`. */
        void load(Schematic& schematic, int index);

        /** The one a fresh instance opens with. */
        inline constexpr int defaultIndex = 0;
    } // namespace Examples
} // namespace SchematicModel
