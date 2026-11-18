#pragma once

#include "../Schematic/Element.h"

namespace Interchange
{
    //==========================================================================
    /** The footprint each part is built on in the KiCad export, worked out from the part
        alone, as a guitar-pedal board is built: through-hole, parts lying on the board at
        their usual size, pots soldered by their pins, op-amps in sockets, and pads to
        solder the wires of everything mounted on the enclosure (jacks, switches, power).

        Footprints are named as KiCad's own libraries name them ("Library:Footprint"), so
        "Update PCB from Schematic" finds them with no library of ours; the one KiCad lacks,
        a 16 mm pot by its pins alone, is Céline's (`celineFootprint`), written beside the
        exported project. Anything else is changed in KiCad: Céline is not a CAD program. */

    /** The footprint of a part, or empty for one a pedal board has no place for (valves,
        transformers, the speaker) and for what is not a part (terminals, nodes, notes). */
    juce::String footprintFor(const SchematicModel::Element& element);

    /** Two pots on one shaft (one label): a dual 16 mm pot. */
    juce::String dualPotFootprint();

    /** Pads in a row to solder `count` wires to: jacks, switches, the power input. */
    juce::String wirePads(int count);

    /** A transistor's lead order on its pads, pad 1 first ("EBC"), as its maker packages it. */
    juce::String pinoutFor(const SchematicModel::Element& element);

    /** What each of Céline's pins is on a transistor ("B", "C", "E"; "D", "G", "S"), in
        Céline's pin order; empty for any other part. */
    juce::StringArray pinRolesFor(SchematicModel::ElementType type);

    /** The text of one of Céline's own footprints ("Celine:..."), or empty for any other. */
    juce::String celineFootprint(const juce::String& id);

    /** The library Céline's own footprints are in, beside an exported project. */
    inline const juce::String celineFootprintLibrary {"Celine"};
} // namespace Interchange
