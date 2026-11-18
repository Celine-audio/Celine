#pragma once

#include "../Schematic/Schematic.h"

namespace Interchange
{
    //==========================================================================
    /** A KiCad project made from a sheet, ready for a PCB (KiCad 9 and later).

        The schematic uses KiCad's standard symbols wherever the library has the part
        (resistors, capacitors, diodes, transistors, op-amps, switches, pots, jacks), copied
        into the file so it opens on its own and matches the library it names. Valves,
        rectifiers, transformers, the LM386 and the LM308 have no standard symbol with
        Céline's pins, so they are drawn from Céline's own artwork, numbered with their real
        socket or package pins.

        Every symbol sits where the part is on the sheet, turned and mirrored the same way,
        and short wires join its pins to where Céline's pins are, so the wiring is kept as
        drawn and every connection lands exactly.

        Every part is given a footprint whose pads its pin numbers match (`footprintFor`, in
        KiCadFootprints.h: a guitar-pedal board, worked out from each part, with no settings),
        so "Update PCB from Schematic" brings everything across. What the sheet draws but a
        board does not have becomes what a board needs instead:
        - Ground and the voltage sources become supply nets (GND, +9V...), brought onto the
          board at pads to solder the supply's wires to;
        - op-amps and the LM386 are wired to their supplies;
        - two op-amps of a dual part share one package, in reading order;
        - pots and switches on one control (one label) are one part with several units;
        - the Input, Output and switches are pads for the enclosure's jacks and toggles to be
          wired to, probes and scopes test points; nodes are net labels, notes are text, boxes
          are drawn rectangles;
        - the LM308 gets the 30 pF compensation capacitor its datasheet and Céline's model
          assume.
        Valves, transformers and speakers are drawn and wired but get no footprint: they are
        not parts of a pedal board. Anything else is done in KiCad. */
    struct KiCadProject
    {
        juce::String schematic; // .kicad_sch
        juce::String project;   // .kicad_pro

        /** What the reader should know: parts exported as something close rather than exact. */
        juce::StringArray warnings;

        /** Where each of Céline's pins went: element index, pin, and the KiCad part and pin
            number it became. What the tests hold KiCad's own netlist to. */
        struct PinOnPart
        {
            int element = 0, pin = 0;
            juce::String reference, number;
        };

        std::vector<PinOnPart> pins;

        /** Céline's own footprints the parts use (file name -> text), for a library beside the
            project, and the footprint table that names it; both empty when none is used. */
        std::map<juce::String, juce::String> footprints;
        juce::String footprintTable;
    };

    KiCadProject toKiCadProject(const SchematicModel::Schematic& sheet, const juce::String& name);

    /** Writes `<folder>/<name>.kicad_sch` and `<name>.kicad_pro`, creating the folder, plus
        Céline's footprint library and its table when a part uses one of its footprints. */
    juce::Result writeKiCadProject(const SchematicModel::Schematic& sheet, const juce::File& folder,
                                   const juce::String& name, juce::StringArray& warnings);

    /** Millimetres per grid square in KiCad files. */
    inline constexpr double kiCadMillimetresPerSquare = 2.54;
} // namespace Interchange
