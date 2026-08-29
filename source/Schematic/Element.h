#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <juce_graphics/juce_graphics.h>

//==============================================================================
/**
    What a placed part on the schematic is.

    The drawing's idea of a component, not the solver's: it knows where it sits,
    which way round it is and what its value says, and nothing about stamping or
    matrices. SchematicBuilder turns a sheet of these into a Circuit.

    Pins are integer offsets from the element's *centre*, on the grid the drawing
    snaps to. Rotating an element rotates its pin offsets, which is the whole of
    what orientation means here.
*/
namespace SchematicModel
{
    //==========================================================================
    /** The parts that can be placed -- deliberately shorter than the engine's
        component set, covering what a pedal or a preamp is made of.

        **The index is written to saved sheets: append, never reorder.** */
    enum class ElementType
    {
        // Terminals -- these name a net rather than contributing a component.
        Ground,
        Input,
        Output,

        // Passives.
        Resistor,
        Capacitor,
        Inductor,

        // The two that stay live after a rebuild.
        Potentiometer,
        Switch,

        // Sources.
        VoltageSource,

        // Semiconductors and valves.
        Diode,
        Transistor,
        Triode,
        OpAmp,

        // Magnetics, appended rather than slotted in beside the other passives.
        Transformer,
        CenterTapTransformer,

        Jfet,
        VacuumDiode,
        Pentode,

        /** A note on the sheet. No pins, so net extraction never sees it. */
        Text,

        /** A box drawn behind everything else, to ring a group of parts.
            Pinless like Text: a thing you read, not one the solver sees. */
        Rectangle,

        /** Single pole, double throw. The engine has no such device -- the
            builder makes it from two switches wired in opposition. */
        Spdt,

        /** A named net. Two carrying the same label are one node however far
            apart they are drawn, so a supply rail reaches across a sheet
            without a wire trailing after it. Ground has always worked this
            way. */
        Node,

        /** An oscilloscope probe: the one part that is *not in the circuit*. It
            stamps nothing, because watching a node must not change it; its pins
            reach net extraction, and the nets they land on are the pair of node
            names to read after the solve. Two pins, so it measures across a part
            as readily as against ground. */
        Scope,
    };

    inline constexpr int numElementTypes = 23;

    /** The smallest a Rectangle may be dragged, in grid squares. Small enough to
        ring one part, large enough that a stray drag can't shrink a box to
        something you then can't find to grab again. */
    inline constexpr int minRectangleSize = 3;

    /** Five, for a centre-tapped transformer: two primary ends, two secondary
        ends and the tap. */
    inline constexpr int maxPinsPerElement = 5;

    /** A pin's position relative to the element's centre, in grid units. */
    struct PinOffset
    {
        int x, y;
    };

    //==========================================================================
    /** Everything about an element type that does not depend on where it was
        placed. One table, read by the net extractor, the builder and the
        renderer alike, so a part's pin count cannot disagree between them. */
    struct ElementInfo
    {
        const char* name;
        int pinCount;
        PinOffset pins[maxPinsPerElement];

        /** What `Element::value` means, and how to show it. Empty for the parts
            that don't carry one. */
        const char* unit;
        const char* valueLabel;

        /** Sensible starting value for a freshly placed part. */
        double defaultValue;

        /** The models this part can be, or empty if it has no such choice.

            `id|Name|Description|Group` records separated by semicolons, the
            group optional. The name goes under the part and into the dropdown;
            the description is the sentence the inspector shows.

            **A description must not contain a semicolon** -- it is the record
            separator, and one inside a sentence silently splits that model in
            two. "Every model record has a name and a description" in
            tests/PluginBasics.cpp enforces it.

            **The id is the file format, and the only field that is.**
            Element::modelIndex stays an index in memory, but it is resolved
            from the id on load and written back as the id on save, so this
            table can be reordered, added to or pruned freely. An id this table
            no longer has falls back to model 0 and says so in the console; see
            Schematic::restoreFromValueTree.

            Ids must be unique within an element type and never empty. Nothing
            at load can catch a duplicate -- the lookup would find the first --
            so "Model ids are unique and well formed" checks it instead. */
        const char* models;

        /** A second value, for the parts that carry two numbers. Last in the
            struct and defaulted, so the rows without one still initialise
            cleanly; null means one value, or none. */
        const char* valueLabelB = nullptr;
        double defaultValueB = 1.0;
    };

    /** Pin geometry note: two-pin parts run vertically by default, pins two grid
        units either side of centre, so the body has room between them. Rotating
        by 90 degrees makes them horizontal, which is how most of a schematic
        ends up drawn. */
    inline const ElementInfo& getElementInfo(ElementType type) noexcept
    {
        static const ElementInfo table[numElementTypes] = {
            // Terminals. One pin, offset so the symbol hangs off the wire end.
            {"Ground", 1, {{0, -2}}, "", "", 0.0, ""},
            {"Input", 1, {{2, 0}}, "", "", 0.0, ""},
            {"Output", 1, {{-2, 0}}, "", "", 0.0, ""},

            // Every value starts at zero rather than at a plausible-looking
            // number: a default that already looks like an answer is one you
            // forget to change. Zero draws red and refuses to build, so an
            // unfilled part is loud instead of silent.
            {"Resistor", 2, {{0, -2}, {0, 2}}, "", "Resistance", 0.0, ""},
            {"Capacitor", 2, {{0, -2}, {0, 2}}, "F", "Capacitance", 0.0, ""},
            {"Inductor", 2, {{0, -2}, {0, 2}}, "H", "Inductance", 0.0, ""},

            // Pin order matches Circuit::addPotentiometer: pin 1, wiper, pin 3.
            {"Potentiometer", 3, {{0, -2}, {3, 0}, {0, 2}}, "", "Max resistance", 0.0, ""},
            {"Switch", 2, {{0, -2}, {0, 2}}, "", "", 0.0, ""},

            {"Voltage source", 2, {{0, -2}, {0, 2}}, "V", "Voltage", 0.0, ""},

            {"Diode", 2, {{0, -2}, {0, 2}}, "", "", 0.0,
             "celine:diode-1n4148|1N4148|Silicon, ~0.6 V|Signal;"
             "celine:diode-germanium|Germanium|1N34A, ~0.33 V and a softer knee|Signal;"
             "celine:diode-schottky|Schottky|1N5817, ~0.30 V and a hard knee|Signal;"
             "celine:led-red|Red LED|~1.6 V|LED;"
             "celine:led-green|Green LED|~1.9 V|LED;"
             "celine:led-blue|Blue LED|~3.2 V|LED;"
             "celine:zener-9v|Zener 9V|Clamps at 9 V in reverse|Zener;"
             "celine:zener-5v1|Zener 5.1V|Clamps at 5.1 V in reverse|Zener;"

             "celine:diode-1n914|1N914|Silicon, ~0.6 V. The same part as the 1N4148|Signal"},

            // Base, collector, emitter -- the order addTransistor() wants.
            {"BJ Transistor", 3, {{-2, 0}, {2, -2}, {2, 2}}, "", "", 0.0,
             "celine:bjt-2n3904|2N3904|Silicon|NPN;"
             "celine:bjt-2n3906|2N3906|Silicon|PNP;"
             "celine:bjt-bc109c|BC109C|Silicon, low noise|NPN;"
             "celine:bjt-2n2222a|2N2222A|Silicon, higher current|NPN;"
             "celine:bjt-ac127|AC127|Germanium|NPN;"
             "celine:bjt-ac128|AC128|Germanium|PNP;"
             "celine:bjt-2n5133|2N5133|Silicon, low noise|NPN"},

            // Plate, grid, cathode -- the order addTriode() wants.
            //
            // Koren fits to JJ datasheets, plus three Dempwolf-Zolzer fits to
            // measurements of individual bottles, named for the tube rather than
            // the type for that reason. Both present the same ports, so mixing
            // them costs nothing. Triode.h has the numbers.
            {"Triode", 3, {{0, -3}, {-3, 0}, {0, 3}}, "", "", 0.0,
             "celine:triode-12ax7-jj|12AX7 JJ|ECC83, MU 100|Koren;"
             "celine:triode-12at7-jj|12AT7 JJ|ECC81, MU 62|Koren;"
             "celine:triode-12ay7-jj|12AY7 JJ|MU 40|Koren;"
             "celine:triode-12au7-jj|12AU7 JJ|ECC82, MU 17|Koren;"
             "celine:triode-12ax7-rsd|12AX7 RSD|MU 103, measured grid current|Dempwolf-Zolzer;"
             "celine:triode-12ax7-rsd2|12AX7 RSD2|MU 100, measured grid current|Dempwolf-Zolzer;"
             "celine:triode-12ax7-ehx|12AX7 EHX|MU 87, measured grid current|Dempwolf-Zolzer;"
             "celine:triode-12ax7-ge|12AX7 GE|MU 103, fitted to 18 measured stages|Koren"},

            // in+, in-, out -- the order addOpAmp() wants. The rails aren't
            // drawn: `value` is the positive one and the negative is ground,
            // which is how a 9 V pedal is wired.
            {"OP-Amp", 3, {{-4, -2}, {-4, 2}, {4, 0}}, "V", "Supply", 0.0,
             "celine:opamp-tl072|TL072|JFET input, 3 MHz. In a large fraction of all pedals;"
             "celine:opamp-jrc4558|JRC4558|The Tube Screamer op-amp, 2.8 MHz;"
             "celine:opamp-ne5532|NE5532|Low-noise studio part, 12 MHz. Quicker and cleaner;"
             "celine:opamp-lm308|LM308|ProCo Rat, 1 MHz. The slowness is the sound;"
             "celine:opamp-ideal|Ideal|A nullor: no gain limit, no bandwidth, no rails, no clipping"},

            // Primary A/B then secondary A/B, the order addTransformer() wants.
            // Two turns counts rather than one ratio: a single box cannot take
            // "1:8", and a step-up comes back out of it as "100m:1".
            {"Transformer", 4, {{-3, -3}, {-3, 3}, {3, -3}, {3, 3}}, "", "Primary turns", 0.0,
             "celine:xfmr-ideal|Ideal|Turns ratio and nothing else. Passes DC, has no bandwidth limit and never "
             "saturates -- put an inductor across the primary yourself for the magnetising "
             "inductance;"
             "celine:xfmr-real|Real|Adds magnetising inductance, leakage and winding resistance, sized from the "
             "turns ratio against an 8 ohm secondary. Blocks DC, and rolls off gently at both "
             "ends -- about -1 dB at 30 Hz and 9 kHz when driven from its own reflected "
             "impedance. The bass end depends on what drives it, the treble end does not",
             "Secondary turns", 0.0},

            // The same, with the tap between the two secondary ends. Both
            // halves share one core, so this is one three-winding transformer
            // rather than two -- which is what a push-pull output stage and a
            // full-wave valve rectifier are both built on.
            {"Transformer (CT)", 5, {{-3, -3}, {-3, 3}, {3, -3}, {3, 0}, {3, 3}}, "",
             "Primary turns", 0.0,
             "celine:xfmr-ct-ideal|Ideal|Turns ratio and nothing else. Passes DC, has no bandwidth limit and never "
             "saturates -- put an inductor across the primary yourself for the magnetising "
             "inductance;"
             "celine:xfmr-ct-real|Real|Adds magnetising inductance, leakage and winding resistance, sized from the "
             "turns ratio against an 8 ohm secondary. Blocks DC, and rolls off gently at both "
             "ends -- about -1 dB at 30 Hz and 9 kHz when driven from its own reflected "
             "impedance. The bass end depends on what drives it, the treble end does not",
             "Secondary turns (total)", 0.0},

            // Drain, gate, source -- the order addJfet() wants. Separate from
            // the BJT rather than a "transistor model": different pins,
            // different symbol, different device model.
            {"JFE Transistor", 3, {{2, -2}, {-2, 0}, {2, 2}}, "", "", 0.0,
             "celine:jfet-j201|J201|Pinch-off -0.7 V|N-channel;"
             "celine:jfet-2n5457|2N5457|Pinch-off -1.5 V|N-channel;"
             "celine:jfet-2n5485|2N5485|Pinch-off -2.0 V|N-channel;"
             "celine:jfet-2n5460|2N5460|Pinch-off -1.5 V|P-channel;"
             "celine:jfet-2n5952|2N5952|Pinch-off -2.4 V|N-channel"},

            // Plate, cathode -- the order addVacuumDiode() wants.
            {"Vacuum Diode", 2, {{0, -3}, {0, 3}}, "", "", 0.0,
             "celine:rect-gz34-jj|GZ34 JJ|5AR4, ~25 V at 250 mA. Least sag|Child-Langmuir;"
             "celine:rect-5u4gb-jj|5U4GB JJ|~50 V at 250 mA|Child-Langmuir;"
             "celine:rect-5y3gt-jj|5Y3GT JJ|~60 V at 125 mA. Saggiest|Child-Langmuir"},

            // Plate, screen, grid, cathode -- the order addPentode() wants.
            //
            // The suppressor is drawn but has no pin: the model assumes g3 sits
            // at the cathode, so a terminal for it could not change anything.
            // Strap the screen to the plate for triode-mode wiring.
            {"Pentode", 4, {{0, -3}, {-3, 0}, {-3, 1}, {0, 3}}, "", "", 0.0,
             "celine:pentode-el34-jj|EL34 JJ|Pentode|Koren;"
             "celine:pentode-6l6gc-jj|6L6GC JJ|Beam tetrode|Koren;"
             "celine:pentode-el84-jj|EL84 JJ|Pentode|Koren;"
             "celine:pentode-kt66-jj|KT66 JJ|Beam tetrode|Koren;"
             "celine:pentode-kt77-jj|KT77 JJ|Pentode|Koren;"
             "celine:pentode-6v6s-jj|6V6S JJ|Beam tetrode|Koren"},

            // No pins: an annotation. `label` is the text itself.
            {"Text", 0, {}, "", "", 0.0, ""},

            // Pinless, and deliberately carrying no value: its size lives in
            // Element::width/height. In `value` it would face the build's
            // zero-value check, and a box must never be able to stop a build.
            // Its "models" are colours.
            {"Box", 0, {}, "", "", 0.0,
             "celine:box-grey|Grey|Add a grey rectangle for better identification;"
             "celine:box-blue|Blue|Add a blue rectangle for better identification;"
             "celine:box-green|Green|Add a green rectangle for better identification;"
             "celine:box-amber|Amber|Add a amber rectangle for better identification;"
             "celine:box-red|Red|Add a red rectangle for better identification;"
             "celine:box-violet|Violet|Add a violet rectangle for better identification"},

            // Common, then the two throws. Pin order is what SchematicBuilder
            // reads: common first, then the throw that is connected when the
            // control is ON, then the one connected when it is OFF.
            {"SPDT switch", 3, {{0, 2}, {-1, -2}, {1, -2}}, "", "", 0.0, ""},

            // A named net. The label is the whole of it, which is why an
            // unlabelled one is a build warning. Its pin sits where Input's
            // does, two squares right: both are drawn as a tag, and a tag points
            // *at* its pin.
            {"Node", 1, {{2, 0}}, "", "", 0.0, ""},

            // A scope probe. Two pins, no value and no model: what it reads is
            // decided by where you hang it, not by anything you type into it.
            {"Scope", 2, {{-2, 2}, {2, 2}}, "", "", 0.0, ""},
        };

        return table[static_cast<int>(type)];
    }

    /** The `Name|Description` records for a type, or empty. */
    inline juce::StringArray getModelRecords(ElementType type)
    {
        const auto* models = getElementInfo(type).models;

        if (models[0] == 0)
            return {};

        return juce::StringArray::fromTokens(juce::String(models), ";", "");
    }

    /** The stable identifier for one model: the first field of its record, and
        the only thing about a model written to a saved sheet. Everything in
        memory still works in indices, but an index is no longer a promise to
        anyone. */
    inline juce::String getModelId(ElementType type, int modelIndex)
    {
        const auto records = getModelRecords(type);

        if (! juce::isPositiveAndBelow(modelIndex, records.size()))
            return {};

        return records[modelIndex].upToFirstOccurrenceOf("|", false, false).trim();
    }

    /** Who made a model. Constant across this table, which is the point: these
        are the models Céline ships, and the `celine:` in their ids is a reserved
        namespace. A user
        library will carry its own author per model, which is where the field
        earns its keep -- two people can both call a tube "Mullard clone", and
        the author plus the hash in their ids is what keeps those apart. */
    inline juce::String getModelAuthor(ElementType, int)
    {
        return juce::String::fromUTF8("Céline");
    }

    /** Which model an id names, or -1 if this element type has no such model.

        -1 is a real answer and callers must handle it: a sheet can legitimately
        name a model that a later build removed or renamed. Schematic::fromValueTree
        falls back to model 0 and says so in the console rather than guessing. */
    inline int findModelById(ElementType type, const juce::String& id)
    {
        if (id.isEmpty())
            return -1;

        const auto records = getModelRecords(type);

        for (int i = 0; i < records.size(); ++i)
            if (records[i].upToFirstOccurrenceOf("|", false, false).trim() == id)
                return i;

        return -1;
    }

    /** Just the short names -- what goes in the dropdown and under the part. */
    inline juce::StringArray getModelChoices(ElementType type)
    {
        juce::StringArray names;

        for (const auto& record : getModelRecords(type))
            names.add(record.fromFirstOccurrenceOf("|", false, false)
                            .upToFirstOccurrenceOf("|", false, false).trim());

        return names;
    }

    /** The sentence for one model, for the inspector. Empty if there isn't one. */
    inline juce::String getModelDescription(ElementType type, int modelIndex)
    {
        const auto records = getModelRecords(type);

        if (! juce::isPositiveAndBelow(modelIndex, records.size()))
            return {};

        // Past the id and the name; what is left is the description, plus the
        // group when the record has one.
        auto tail = records[modelIndex].fromFirstOccurrenceOf("|", false, false)
                                       .fromFirstOccurrenceOf("|", false, false);

        // The group, when there is one, is the last field -- so the description
        // is everything between the name and it.
        return (tail.contains("|") ? tail.upToLastOccurrenceOf("|", false, false) : tail).trim();
    }

    /** Which heading this model sits under in the dropdown, or empty. A
        *display* grouping only: item ids stay the model index whatever order
        the groups put them in, which is what lets one polarity's parts be
        non-contiguous in the table. */
    inline juce::String getModelGroup(ElementType type, int modelIndex)
    {
        const auto records = getModelRecords(type);

        if (! juce::isPositiveAndBelow(modelIndex, records.size()))
            return {};

        auto tail = records[modelIndex].fromFirstOccurrenceOf("|", false, false)
                                       .fromFirstOccurrenceOf("|", false, false);

        return tail.contains("|") ? tail.fromLastOccurrenceOf("|", false, false).trim()
                                  : juce::String();
    }

    /** Every group in the order they first appear, for building a menu. */
    inline juce::StringArray getModelGroups(ElementType type)
    {
        juce::StringArray groups;

        for (int i = 0; i < getModelRecords(type).size(); ++i)
        {
            const auto group = getModelGroup(type, i);

            if (group.isNotEmpty() && ! groups.contains(group))
                groups.add(group);
        }

        return groups;
    }

    /** True for a PNP transistor or a P-channel FET.

        Read from the *group* -- the dropdown heading, which for these parts is
        their polarity and nothing else -- so the heading a part is listed under
        and the direction its arrow points cannot disagree.

        Never from the part number: an earlier version did, and renaming the
        models to real part numbers silently turned every transistor into an
        NPN. A wrong arrow is a wrong schematic. */
    inline bool isReversePolarity(ElementType type, int modelIndex)
    {
        const auto group = getModelGroup(type, modelIndex);
        return group.startsWithIgnoreCase("PNP") || group.startsWithIgnoreCase("P-channel");
    }

    /** True for the parts that become a Switch control: one contact or two.
        Everything that asks -- the inspector's toggle, the control order, the
        ganging -- wants both kinds, so they ask here rather than each naming the
        two types and forgetting one when a third arrives. */
    inline bool isSwitch(ElementType type) noexcept
    {
        return type == ElementType::Switch || type == ElementType::Spdt;
    }

    /** How a potentiometer's resistance follows its knob. Linear by default: a
        log taper is a deliberate choice about how a control feels. **The
        integer value is the saved format.** */
    enum class Taper
    {
        Linear = 0,
        Logarithmic = 1,
        ReverseLogarithmic = 2,

        // The numbered audio tapers, named the way the trade names them: the
        // number is the percentage of the track the wiper has passed at half
        // rotation, which is how a pot's curve is actually specified. A 10A
        // reads 10% at noon; a 30A is nearly linear.
        //
        // These follow the two-segment law a real audio pot is *built* from --
        // see CircuitComponents::Potentiometer::setPosition -- not the square
        // law Logarithmic uses. The difference is at the bottom of the travel
        // and it is large: a 10A reads about 4% a fifth of the way round, where
        // the equivalent power law reads a twentieth of that.
        Log5A = 3,
        Log10A = 4,
        Log15A = 5,
        Log20A = 6,
        Log30A = 7,
    };

    /** How many there are. Used to clamp a taper read from a file, which is why
        it lives next to the enum rather than being counted at each call. */
    inline constexpr int numTapers = 8;

    inline juce::StringArray getTaperNames()
    {
        return {"Linear", "Logarithmic", "Reverse logarithmic",
                "Log 5A", "Log 10A", "Log 15A", "Log 20A", "Log 30A"};
    }

    /** How a real potentiometer is marked.

        A is audio (logarithmic), B is linear, C is reverse audio -- the modern
        convention, and the one every guitar part is sold under: an "A500K" is
        the log pot in a Les Paul. Older European parts used A and B the other
        way round, which is why this is written down rather than inferred.

        The numbered tapers have no single letter -- the percentage is a
        datasheet figure, not a marking -- so they take the form the trade says
        out loud: "10A". Anything longer than one character is separated from the
        value on the sheet, since "A250K" is a marking somebody would recognise
        and "10A250K" is not.

        *Not* the saved format: `Taper`'s integer value is. Changing a code here
        is cosmetic. */
    inline juce::String getTaperCode(Taper taper)
    {
        switch (taper)
        {
            case Taper::Logarithmic:        return "A";
            case Taper::ReverseLogarithmic: return "C";
            case Taper::Log5A:              return "5A";
            case Taper::Log10A:             return "10A";
            case Taper::Log15A:             return "15A";
            case Taper::Log20A:             return "20A";
            case Taper::Log30A:             return "30A";
            case Taper::Linear:             break;
        }

        return "B";
    }

    /** The inverse. False when the text names no taper we have. */
    inline bool taperFromCode(const juce::String& code, Taper& out)
    {
        for (int i = 0; i < numTapers; ++i)
        {
            const auto taper = static_cast<Taper>(i);

            if (getTaperCode(taper).equalsIgnoreCase(code.trim()))
            {
                out = taper;
                return true;
            }
        }

        return false;
    }

    //==========================================================================
    /**
        A part, placed.

        `value` carries the one number most parts have -- ohms, farads, henries,
        volts. Parts chosen from a list of models use `modelIndex` instead, and
        the terminals use neither.
    */
    struct Element
    {
        int id = 0;
        ElementType type = ElementType::Resistor;

        /** Centre position, in grid units. */
        int x = 0, y = 0;

        /** Quarter turns clockwise, 0 to 3. */
        int orientation = 0;

        /** Mirrored about its own vertical axis. Rotation alone can't put a
            triode's grid on the right while keeping its plate at the top, and
            that is exactly what a tidy layout often wants. */
        bool mirrored = false;

        double value = 0.0;

        /** The second number, for the parts that carry two. Defaults to 1 rather
            than 0 so that a transformer saved before this existed -- where
            `value` was the whole ratio -- still reads as value:1. */
        double valueB = 1.0;

        int modelIndex = 0;

        /** The model id this element was saved with, when this build could not
            resolve it. Empty in every ordinary case.

            The element builds as model 0 but remembers what the sheet asked for,
            and toValueTree() writes this back instead of the substitute --
            otherwise opening a sheet whose model you do not have and saving
            would lose the reference for good. That save is often nobody's
            decision: a host writes session state on its own, so a sheet in a DAW
            project would lose it just by being opened and closed.

            Cleared by choosing any model in the inspector. */
        juce::String unresolvedModelId;

        /** Shown next to the part, and -- for a Potentiometer or Switch -- the
            name its live control gets in the UI. Optional. */
        juce::String label;

        /** Potentiometer only: how the resistance follows the knob. */
        Taper taper = Taper::Linear;

        /** Potentiometer and Switch only: where this control sits in the strip.

            Lower comes first, equal values keep the order they were drawn in, so
            leaving everything at zero changes nothing. A number rather than a
            drag because the strip scrolls, and dragging something to a position
            you cannot see is worse than typing where it goes. */
        int controlOrder = 0;

        /** Capacitor only. An electrolytic: pin 0 is the marked positive
            terminal, it has a non-zero ESR, and the build checks it isn't
            reverse-biased at the bias point -- which is the point of saying so,
            since a backwards electrolytic is a wiring error the drawing can't
            otherwise show. */
        bool polarised = false;

        /** Switch only: its position at build time. */
        bool closed = true;

        /** Potentiometer only: where the knob sits, 0 to 1.

            Half rather than zero -- the one place the table's zero-means-unfilled
            rule does not apply, since a knob position is not a number you forgot
            to type. Seeded into the strip at build time and written back as the
            knob turns, so a pot keeps its position across rebuilds instead of
            inheriting whatever the last control in that slot was left at. */
        double controlPosition = 0.5;

        /** Where this part's live control stands, 0 to 1, whichever kind it is
            -- one currency, so the strip, the seeding and the write-back need
            not know that a switch stores a bool and a pot a double. Anything
            that is not a control reads as centred. */
        float getControlPosition() const noexcept
        {
            if (isSwitch(type))
                return closed ? 1.0f : 0.0f;

            return type == ElementType::Potentiometer ? static_cast<float>(controlPosition) : 0.5f;
        }

        void setControlPosition(float position) noexcept
        {
            if (isSwitch(type))
                closed = position >= 0.5f;
            else if (type == ElementType::Potentiometer)
                controlPosition = juce::jlimit(0.0, 1.0, static_cast<double>(position));
        }

        /** Output only: the speaker cabinet, and whether it is in circuit.

            Deliberately *not* part of the circuit. A cabinet is a fixed linear
            filter, and the engine exists to solve nonlinear networks: putting
            an impulse response in the matrix would buy nothing, cost a
            factorisation, and make switching it a rebuild. So it hangs off the
            Output terminal -- which is where it sits in the signal path, and a
            sheet with no output has nothing to filter anyway.

            The path travels **in the document**, unlike the preset folder,
            which deliberately does not -- see PresetLibrary. The difference is
            what the two describe. A preset folder is a fact about the machine
            someone is sitting at, and pointing a friend's menu at a directory
            they haven't got is nothing but damage. An impulse response is part
            of what the patch *sounds like*, so a sheet has to carry which one it
            meant; a machine without that file says so and passes the signal
            through, rather than quietly forgetting there was a cab at all. */
        bool cabEnabled = false;
        juce::String cabFile;

        /** Rectangle only: its size in grid squares. The one pair here that does
            not start at zero -- the zero defaults exist to make an unfilled part
            loud, and a zero-sized box would simply be invisible. */
        int width = 14, height = 10;

        /** Scope only: what the picture is drawn against.

            In their own fields rather than in `value`, for the reason the
            rectangle's size is: a viewing preference must not be able to refuse
            to build a working amplifier. Nothing here reaches the matrix.

            Auto-scaling by default, which is right until you are comparing two
            things -- a window that resizes to whatever it holds always looks
            full, so a stage that lost 20 dB looks like one that did not. */
        bool scopeAutoScale = true;

        /** Volts at the bottom and top of the picture, when not auto-scaling.
            Not zero-defaulted: unlike a part's value, zero here is an ordinary
            thing to want. */
        double scopeMin = -5.0, scopeMax = 5.0;

        /** How wide a slice of time the picture covers, in seconds. Per scope,
            so a sheet can watch a 20 kHz ripple on one probe and an envelope
            settling on another. */
        double scopeSeconds = 0.04;

        /** A point in the part's own frame, placed on the sheet: mirrored,
            rotated and moved to where the part sits. Shared with getPinPosition
            so that anything else in the part's frame -- a Node's tag, which has
            corners rather than pins -- turns exactly the way the pins do. */
        juce::Point<int> localToSheet(int localX, int localY) const noexcept
        {
            // Mirror first, in the part's own frame, then rotate. That order is
            // what makes "flip" mean the same thing whichever way the part is
            // turned -- it swaps the part's own left and right, not the sheet's.
            int px = mirrored ? -localX : localX;
            int py = localY;

            // Quarter turns clockwise: (x, y) -> (-y, x).
            for (int turn = 0; turn < (orientation & 3); ++turn)
            {
                const int rotatedX = -py;
                py = px;
                px = rotatedX;
            }

            return {x + px, y + py};
        }

        /** Where pin `index` sits in grid coordinates, orientation applied. */
        juce::Point<int> getPinPosition(int index) const noexcept
        {
            const auto& info = getElementInfo(type);
            jassert(index >= 0 && index < info.pinCount);

            const auto offset = info.pins[index];
            return localToSheet(offset.x, offset.y);
        }

        /** Copies everything that makes this part *this part* -- value, model,
            label, settings -- and nothing about where it sits. Identity and
            placement stay with the target.

            One function, so a member added to this struct has one place to be
            remembered rather than being dropped by whoever forgot. */
        void copyPropertiesTo(Element& target) const
        {
            target.type = type;
            target.value = value;
            target.valueB = valueB;
            target.modelIndex = modelIndex;
            target.unresolvedModelId = unresolvedModelId;
            target.label = label;
            target.taper = taper;
            target.controlOrder = controlOrder;
            target.polarised = polarised;
            target.closed = closed;
            target.controlPosition = controlPosition;
            target.cabEnabled = cabEnabled;
            target.cabFile = cabFile;
            target.width = width;
            target.height = height;
            target.scopeAutoScale = scopeAutoScale;
            target.scopeMin = scopeMin;
            target.scopeMax = scopeMax;
            target.scopeSeconds = scopeSeconds;
        }

        int getPinCount() const noexcept { return getElementInfo(type).pinCount; }

        /** True for the parts that carry two numbers instead of one. */
        bool hasSecondValue() const noexcept
        {
            const auto* caption = getElementInfo(type).valueLabelB;
            return caption != nullptr && caption[0] != 0;
        }

        /** Primary turns over secondary turns. Guarded because the secondary box
            is free text and 0 would divide the circuit by zero rather than
            merely being wrong. */
        double getTurnsRatio() const noexcept { return valueB > 0.0 ? value / valueB : value; }

        /** True for the parts whose value the user can type. */
        bool hasNumericValue() const noexcept { return getElementInfo(type).valueLabel[0] != 0; }

        /** True for the parts whose model is picked from a list. */
        bool hasModelChoice() const noexcept { return getElementInfo(type).models[0] != 0; }

        /** What a Text note reads on the sheet: its label, or a placeholder so
            that an empty one is still visible and still selectable. Anything
            else has no text of its own. */
        juce::String getDisplayText() const
        {
            if (type != ElementType::Text)
                return {};

            return label.isNotEmpty() ? label : "Text";
        }

        /** Half the width of a Text note, in grid squares -- an approximation of
            where the glyphs end, and deliberately the only one, since the
            renderer and the hit box both call it. Without it a pinless element
            collapses to a 3x3 target under the middle of its own text. */
        int getTextHalfWidth() const
        {
            return juce::jmax(2, juce::roundToInt(0.32 * getDisplayText().length()) + 1);
        }

        //======================================================================
        // The Node tag. A node is a name and nothing else, so its symbol is a
        // *reading* of its own state -- the tag is as wide as what it says --
        // and no SVG could hold it. Same reason Text and Rectangle are in code.

        /** What a Node's tag reads: its name, or a red question mark when nobody
            has named one -- the same red the sheet uses for a value left at
            zero, meaning the same thing. */
        juce::String getNodeDisplayText() const
        {
            if (type != ElementType::Node)
                return {};

            return label.isNotEmpty() ? label : "?";
        }

        /** How wide a Node's tag body is, in grid squares -- the boxed part,
            without the point. Same contract as getTextHalfWidth(): the renderer,
            the hit box and the sheet's bounds all come through here. */
        int getNodeBodyWidth() const
        {
            return juce::jmax(2, juce::roundToInt(0.62 * getNodeDisplayText().length()) + 1);
        }

        /** The tag in the part's own frame, before rotation: a box `body` wide
            with a one-square point on its right end, so the tip lands on the pin
            at {2, 0}. One square of point against one of half-height is a 45
            degree tip, the same angle the wire handles use. */
        juce::Rectangle<int> getNodeLocalBounds() const noexcept
        {
            const int body = getNodeBodyWidth();
            return {1 - body, -1, body + 1, 2};
        }

        /** The same tag, placed and turned: what it covers on the sheet.
            Corners through localToSheet so it turns exactly as the pin does. */
        juce::Rectangle<int> getNodeBounds() const noexcept
        {
            const auto local = getNodeLocalBounds();

            const auto a = localToSheet(local.getX(), local.getY());
            const auto b = localToSheet(local.getRight(), local.getBottom());

            // Min/max by hand, and by now for the familiar reason: a rotation
            // can put `b` above or left of `a`, and a Rectangle built from two
            // points the wrong way round comes out empty.
            const int left = juce::jmin(a.x, b.x), right = juce::jmax(a.x, b.x);
            const int top = juce::jmin(a.y, b.y), bottom = juce::jmax(a.y, b.y);

            return {left, top, right - left, bottom - top};
        }

        /** The grid square a Rectangle covers, centred like everything else.

            The renderer, the hit test and Schematic::getElementBounds all call
            this rather than each deriving a box from x/y/width/height, so the box
            you click is the box you see. Clamped here, so a width read from a
            file cannot produce a rectangle too small to grab.

            Rectangle<int>::withCentre is the exact inverse of getCentreX/Y at any
            size, odd ones included, which is what lets a corner drag pin the
            opposite corner without drift. */
        juce::Rectangle<int> getRectangleBounds() const noexcept
        {
            return juce::Rectangle<int>(0, 0, juce::jmax(minRectangleSize, width),
                                        juce::jmax(minRectangleSize, height))
                .withCentre({x, y});
        }
    };
} // namespace SchematicModel
