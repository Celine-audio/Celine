#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <juce_graphics/juce_graphics.h>

//==============================================================================
/** A part placed on the schematic: position, orientation and values, nothing about the
    solver. SchematicBuilder turns a sheet of these into a Circuit.

    Pins are integer offsets from the element's centre, in grid units; orientation
    rotates them. */
namespace SchematicModel
{
    //==========================================================================
    /** The placeable part types. The index is saved in sheets: append, never reorder. */
    enum class ElementType
    {
        // Terminals: these name a net rather than add a component.
        Ground,
        Input,
        Output,

        // Passives.
        Resistor,
        Capacitor,
        Inductor,

        // Live after a rebuild.
        Potentiometer,
        Switch,

        // Sources.
        VoltageSource,

        // Semiconductors and valves.
        Diode,
        Transistor,
        Triode,
        OpAmp,

        // Magnetics.
        Transformer,
        CenterTapTransformer,

        Jfet,
        VacuumDiode,
        Pentode,

        /** A note. No pins. */
        Text,

        /** A box drawn behind the circuit to group parts. No pins. */
        Rectangle,

        /** A single-pole changeover switch with two or three throws, in ON-ON, ON-OFF-ON or
            ON-ON-ON action (see SwitchAction). More poles: several switches with one label.
            Named Spdt for file compatibility. */
        Spdt,

        /** A named net: nodes with the same label are connected, like Ground. */
        Node,

        /** A probe that stamps nothing; its two pins' nets are read after each solve. */
        Scope,

        /** An enhancement MOSFET. */
        Mosfet,

        /** An LM386-style power amp: op-amp pins plus gain pins 1 and 8. Its feedback is
            internal, so it is not an op-amp model. */
        PowerAmp,
    };

    inline constexpr int numElementTypes = 25;

    /** Smallest Rectangle size, in grid squares. */
    inline constexpr int minRectangleSize = 3;

    /** Most pins of any fixed type: a centre-tapped transformer's five. */
    inline constexpr int maxPinsPerElement = 5;

    /** A pin's offset from the element's centre, in grid units. */
    struct PinOffset
    {
        int x, y;
    };

    //==========================================================================
    /** Everything about an element type that does not depend on placement. The one table
        read by net extraction, the builder and the renderer. */
    struct ElementInfo
    {
        const char* name;
        int pinCount;
        PinOffset pins[maxPinsPerElement];

        /** Unit of `Element::value`; empty for parts without one. */
        const char* unit;
        const char* valueLabel;

        /** Initial value of a new part. */
        double defaultValue;

        /** The models this part can be, or empty.

            `id|Name|Description|Group` records separated by semicolons; group optional. Name
            is the part number shown in the dropdown and under the part; group is the dropdown
            heading; description is shown in the inspector (may be empty). A description must
            not contain a semicolon.

            Only the id is saved; modelIndex is resolved from it on load, so the list can be
            reordered freely. An unknown id falls back to model 0 with a console message. Ids
            must be unique per type and non-empty (checked by "Model ids are unique and well
            formed"). */
        const char* models;

        /** Label of a second value, or null for none. */
        const char* valueLabelB = nullptr;
        double defaultValueB = 1.0;

        /** True when the second value is a voltage at or below ground (an op-amp's negative
            rail), where zero is valid: never shown as unset, and entered as a size below
            ground. */
        bool valueBIsRail = false;
    };

    /** Two-pin parts are vertical by default, pins two units either side of centre. */
    inline const ElementInfo& getElementInfo(ElementType type) noexcept
    {
        static const ElementInfo table[numElementTypes] = {
            // Terminals: one pin.
            {"Ground", 1, {{0, -2}}, "", "", 0.0, ""},
            {"Input", 1, {{2, 0}}, "", "", 0.0, ""},
            {"Output", 1, {{-2, 0}}, "", "", 0.0, ""},

            // Values default to zero, so an unfilled part shows red and refuses to build.
            {"Resistor", 2, {{0, -2}, {0, 2}}, "", "Resistance", 0.0, ""},
            {"Capacitor", 2, {{0, -2}, {0, 2}}, "F", "Capacitance", 0.0, ""},
            {"Inductor", 2, {{0, -2}, {0, 2}}, "H", "Inductance", 0.0, ""},

            // Pin order as Circuit::addPotentiometer: pin 1, wiper, pin 3.
            {"Potentiometer", 3, {{0, -2}, {3, 0}, {0, 2}}, "", "Max resistance", 0.0, ""},
            {"Switch", 2, {{0, -2}, {0, 2}}, "", "", 0.0, ""},

            {"Voltage source", 2, {{0, -2}, {0, 2}}, "V", "Voltage", 0.0, ""},

            {"Diode", 2, {{0, -2}, {0, 2}}, "", "", 0.0,
             "celine:diode-1n4148|1N4148|~0.6 V|Silicon;"
             "celine:diode-germanium|1N34A|~0.33 V, soft knee|Germanium;"
             "celine:diode-schottky|1N5817|~0.1 V at 1 mA, 0.17 at 10 mA|Schottky;"
             "celine:led-red|WP7113ID|Red, ~1.65 V|LED;"
             "celine:led-green|WP7113GD|Green, ~1.85 V|LED;"
             "celine:led-blue|WP7113QBC|Blue, ~2.55 V|LED;"
             "celine:zener-9v|BZX55C9V1|9.1 V Zener, sharp knee|Zener;"
             "celine:zener-5v1|BZX55C5V1|5.1 V Zener, soft knee|Zener;"
             "celine:diode-1n914|1N914|Same as the 1N4148|Silicon;"
             "celine:diode-bat41|BAT41|~0.4 V, soft knee|Schottky;"
             "celine:diode-bat46|BAT46|~0.26 V, sharp knee|Schottky;"
             "celine:diode-1n4001|1N4001|Rectifier, ~0.57 V, slow recovery|Silicon;"
             "celine:zener-4v3|BZX55C4V3|4.3 V Zener, soft knee|Zener;"
             "celine:zener-3v9|BZX55C3V9|3.9 V Zener, soft knee|Zener"},

            // Base, collector, emitter (addTransistor() order).
            {"BJ Transistor", 3, {{-2, 0}, {2, -2}, {2, 2}}, "", "", 0.0,
             "celine:bjt-2n3904|2N3904|Silicon|NPN;"
             "celine:bjt-2n3906|2N3906|Silicon|PNP;"
             "celine:bjt-bc109c|BC109C|Grade C, hFE 420-800. Also BC547C-550C|NPN;"
             "celine:bjt-2n2222a|2N2222A|Silicon, higher current|NPN;"
             "celine:bjt-ac127|AC127|Germanium|NPN;"
             "celine:bjt-ac128|AC128|Germanium|PNP;"
             "celine:bjt-2n5133|2N5133|Silicon, low noise|NPN;"
             "celine:bjt-bc108a|BC108A|Grade A, hFE 110-220. Also BC547A, BC548A|NPN;"
             "celine:bjt-bc108b|BC108B|Grade B, hFE 200-450. Also BC547B-550B|NPN;"
             "celine:bjt-2n5088|2N5088|Low noise, hFE 300-900|NPN;"
             "celine:bjt-2n5089|2N5089|Low noise, hFE 400-1200|NPN;"
             "celine:bjt-mpsa18|MPSA18|Low noise, hFE 500-1500|NPN;"
             "celine:bjt-mpsa13|MPSA13|Darlington, Vbe ~1.1 V|NPN;"
             "celine:bjt-nte103|NTE103|Germanium, hFE ~95 at 1 mA|NPN;"
             "celine:bjt-nte102|NTE102|Germanium, hFE ~95 at 1 mA|PNP;"
             "celine:bjt-2sc2240gr|2SC2240GR|Low noise, hFE 200-400|NPN;"
             "celine:bjt-2sc2240bl|2SC2240BL|Low noise, hFE 350-700|NPN"},

            // Plate, grid, cathode (addTriode() order). Koren fits to JJ datasheets plus
            // Dempwolf-Zolzer fits to measured valves; see Triode.h.
            {"Triode", 3, {{0, -3}, {-3, 0}, {0, 3}}, "", "", 0.0,
             "celine:triode-12ax7-jj|12AX7 JJ|ECC83, MU 100|Koren;"
             "celine:triode-12at7-jj|12AT7 JJ|ECC81, MU 62|Koren;"
             "celine:triode-12ay7-jj|12AY7 JJ|MU 40|Koren;"
             "celine:triode-12au7-jj|12AU7 JJ|ECC82, MU 17|Koren;"
             "celine:triode-12ax7-rsd|12AX7 RSD|MU 103, measured grid current|Dempwolf-Zolzer;"
             "celine:triode-12ax7-rsd2|12AX7 RSD2|MU 100, measured grid current|Dempwolf-Zolzer;"
             "celine:triode-12ax7-ehx|12AX7 EHX|MU 87, measured grid current|Dempwolf-Zolzer;"
             "celine:triode-12ax7-ge|12AX7 GE|MU 103, fitted to 18 measured stages|Koren"},

            // in+, in-, out (addOpAmp() order). `value` is the positive rail, `valueB` the
            // negative (0 = ground). See valueBIsRail.
            {"OP-Amp", 3, {{-4, -2}, {-4, 2}, {4, 0}}, "V", "Supply +", 0.0,
             "celine:opamp-tl072|TL071/TL072|JFET input, 3 MHz;"
             "celine:opamp-jrc4558|JRC4558|Distortion pedal OpAmp, 3 MHz;"
             "celine:opamp-ne5532|NE5532|Low noise, 12 MHz;"
             "celine:opamp-lm308|LM308|Distortion pedal OpAmp, 1 MHz;"
             "celine:opamp-ideal|Ideal|No gain limit, bandwidth or clipping;"
             "celine:opamp-lm741|LM741|General purpose, 0.9 MHz",
             "Supply -", 0.0, true},

            // Primary A/B then secondary A/B (addTransformer() order). Two turn counts rather than
            // one ratio.
            {"Transformer", 4, {{-3, -3}, {-3, 3}, {3, -3}, {3, 3}}, "", "Primary turns", 0.0,
             "celine:xfmr-ideal|Ideal|Turns ratio only. Passes DC;"
             "celine:xfmr-real|Real|Adds inductance and winding losses. Blocks DC",
             "Secondary turns", 0.0},

            // As above, with the secondary's centre tap; one three-winding transformer.
            {"Transformer (CT)", 5, {{-3, -3}, {-3, 3}, {3, -3}, {3, 0}, {3, 3}}, "",
             "Primary turns", 0.0,
             "celine:xfmr-ct-ideal|Ideal|Turns ratio only. Passes DC;"
             "celine:xfmr-ct-real|Real|Adds inductance and winding losses. Blocks DC",
             "Secondary turns (total)", 0.0},

            // Drain, gate, source (addJfet() order).
            {"JFE Transistor", 3, {{2, -2}, {-2, 0}, {2, 2}}, "", "", 0.0,
             "celine:jfet-j201|J201|Pinch-off -0.69 V|N-channel;"
             "celine:jfet-2n5457|2N5457|Pinch-off -1.25 V|N-channel;"
             "celine:jfet-2n5485|2N5485|Pinch-off -2.16 V|N-channel;"
             "celine:jfet-2n5460|2N5460|Pinch-off +2.16 V|P-channel;"
             "celine:jfet-2n5952|2N5952|Pinch-off -2.4 V|N-channel"},

            // Plate, cathode (addVacuumDiode() order).
            {"Vacuum Diode", 2, {{0, -3}, {0, 3}}, "", "", 0.0,
             "celine:rect-gz34-jj|GZ34 JJ|5AR4, ~25 V at 250 mA. Least sag|Child-Langmuir;"
             "celine:rect-5u4gb-jj|5U4GB JJ|~50 V at 250 mA|Child-Langmuir;"
             "celine:rect-5y3gt-jj|5Y3GT JJ|~60 V at 125 mA. Saggiest|Child-Langmuir"},

            // Plate, screen, grid, cathode (addPentode() order). No suppressor pin: the model
            // puts g3 at the cathode.
            {"Pentode", 4, {{0, -3}, {-3, 0}, {-3, 1}, {0, 3}}, "", "", 0.0,
             "celine:pentode-el34-jj|EL34 JJ|Pentode|Koren;"
             "celine:pentode-6l6gc-jj|6L6GC JJ|Beam tetrode|Koren;"
             "celine:pentode-el84-jj|EL84 JJ|Pentode|Koren;"
             "celine:pentode-kt66-jj|KT66 JJ|Beam tetrode|Koren;"
             "celine:pentode-kt77-jj|KT77 JJ|Pentode|Koren;"
             "celine:pentode-6v6s-jj|6V6S JJ|Beam tetrode|Koren;"
             "celine:pentode-kt88-jj|KT88 JJ|Beam tetrode|Koren"},

            // No pins. `label` is the text.
            {"Text", 0, {}, "", "", 0.0, ""},

            // No pins and no value: its size is in width/height, so it can never fail the
            // build's zero-value check. Its models are colours.
            {"Box", 0, {}, "", "", 0.0,
             "celine:box-grey|Grey|Add a grey rectangle for better identification;"
             "celine:box-blue|Blue|Add a blue rectangle for better identification;"
             "celine:box-green|Green|Add a green rectangle for better identification;"
             "celine:box-amber|Amber|Add a amber rectangle for better identification;"
             "celine:box-red|Red|Add a red rectangle for better identification;"
             "celine:box-violet|Violet|Add a violet rectangle for better identification"},

            // The two-throw layout. Actual pins come from Element::getSwitchPinOffset.
            {"xPyT switch", 3, {{0, 2}, {-1, -2}, {1, -2}}, "", "", 0.0, ""},

            // A named net; unlabelled is a build warning. Pin two squares right, like Input.
            {"Node", 1, {{2, 0}}, "", "", 0.0, ""},

            // A scope: two pins, no value, no model.
            {"Scope", 2, {{-2, 2}, {2, 2}}, "", "", 0.0, ""},

            // Drain, gate, source (addMosfet() order); same pins as the JFET.
            {"MOSFE Transistor", 3, {{2, -2}, {-2, 0}, {2, 2}}, "", "", 0.0,
             "celine:mosfet-2n7000|2N7000|Threshold 2.1 V. Also BS170|N-channel;"
             "celine:mosfet-bs250|BS250|Threshold -3.2 V|P-channel"},

            // in+, in-, out, then pins 1 and 8 (addPowerAmp() order). `value` is the supply;
            // ground is the other rail.
            {"Power amp", 5, {{-4, -2}, {-4, 2}, {4, 0}, {-1, -4}, {0, -4}}, "V", "Supply", 0.0,
             "celine:poweramp-lm386|LM386|"},
        };

        return table[static_cast<int>(type)];
    }

    /** The model records for a type, or empty. */
    inline juce::StringArray getModelRecords(ElementType type)
    {
        const auto* models = getElementInfo(type).models;

        if (models[0] == 0)
            return {};

        return juce::StringArray::fromTokens(juce::String(models), ";", "");
    }

    /** A model's stable id: the first field of its record, and what saved sheets store. */
    inline juce::String getModelId(ElementType type, int modelIndex)
    {
        const auto records = getModelRecords(type);

        if (! juce::isPositiveAndBelow(modelIndex, records.size()))
            return {};

        return records[modelIndex].upToFirstOccurrenceOf("|", false, false).trim();
    }

    /** Who made a model. Always Céline for this table (`celine:` ids are reserved). */
    inline juce::String getModelAuthor(ElementType, int)
    {
        return juce::String::fromUTF8("Céline");
    }

    /** The model an id names, or -1 if none (e.g. removed in this build). */
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

    /** Short model names, for the dropdown and the part caption. */
    inline juce::StringArray getModelChoices(ElementType type)
    {
        juce::StringArray names;

        for (const auto& record : getModelRecords(type))
            names.add(record.fromFirstOccurrenceOf("|", false, false)
                            .upToFirstOccurrenceOf("|", false, false).trim());

        return names;
    }

    /** A model's description for the inspector, or empty. */
    inline juce::String getModelDescription(ElementType type, int modelIndex)
    {
        const auto records = getModelRecords(type);

        if (! juce::isPositiveAndBelow(modelIndex, records.size()))
            return {};

        // After the id and name: the description, plus the group if any.
        auto tail = records[modelIndex].fromFirstOccurrenceOf("|", false, false)
                                       .fromFirstOccurrenceOf("|", false, false);

        // The group, if present, is the last field.
        return (tail.contains("|") ? tail.upToLastOccurrenceOf("|", false, false) : tail).trim();
    }

    /** A model's dropdown heading, or empty. Display only: item ids stay model indices. */
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

    /** Every group in order of first appearance. */
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

    /** True for a PNP transistor or P-channel FET. Read from the group, never the part
        number. */
    inline bool isReversePolarity(ElementType type, int modelIndex)
    {
        const auto group = getModelGroup(type, modelIndex);
        return group.startsWithIgnoreCase("PNP") || group.startsWithIgnoreCase("P-channel");
    }

    /** True for the parts that become a switch control. */
    inline bool isSwitch(ElementType type) noexcept
    {
        return type == ElementType::Switch || type == ElementType::Spdt;
    }

    /** How a changeover switch moves:
        - ON-ON: two positions, throw A or throw B;
        - ON-OFF-ON: three positions, the middle connects nothing;
        - ON-ON-ON: three positions and three throws.

        The integer value is saved. */
    enum class SwitchAction
    {
        OnOn = 0,
        OnOffOn = 1,
        OnOnOn = 2,
    };

    inline constexpr int numSwitchActions = 3;

    inline juce::StringArray getSwitchActionNames()
    {
        return {"ON-ON", "ON-OFF-ON", "ON-ON-ON"};
    }

    /** How a pot's resistance follows its position. The integer value is saved. */
    enum class Taper
    {
        Linear = 0,
        Logarithmic = 1,
        ReverseLogarithmic = 2,

        // Numbered audio tapers: the number is the percentage of resistance at half
        // rotation (10A reads 10% at noon). They follow the two-segment law of real audio
        // pots (see CircuitComponents::Potentiometer::setPosition), not Logarithmic's square
        // law.
        Log5A = 3,
        Log10A = 4,
        Log15A = 5,
        Log20A = 6,
        Log30A = 7,
    };

    /** Number of tapers, for clamping values read from files. */
    inline constexpr int numTapers = 8;

    inline juce::StringArray getTaperNames()
    {
        return {"Linear", "Logarithmic", "Reverse logarithmic",
                "Log 5A", "Log 10A", "Log 15A", "Log 20A", "Log 30A"};
    }

    /** A pot's taper marking: A = audio, B = linear, C = reverse audio; numbered tapers
        are written "10A". Display only (the enum value is what is saved). */
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

    /** The inverse. False if the text names no taper. */
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
    /** A placed part. `value` is the main number (ohms, farads, henries, volts); parts
        with models use `modelIndex`; terminals use neither. */
    struct Element
    {
        int id = 0;
        ElementType type = ElementType::Resistor;

        /** Centre, in grid units. */
        int x = 0, y = 0;

        /** Quarter turns clockwise, 0 to 3. */
        int orientation = 0;

        /** Mirrored about its own vertical axis. */
        bool mirrored = false;

        double value = 0.0;

        /** The second number, for parts with two. Defaults to 1 so old transformer sheets
            (ratio in `value`) read as value:1. */
        double valueB = 1.0;

        int modelIndex = 0;

        /** The saved model id when this build doesn't know it; empty otherwise. The part builds
            as model 0 but this id is saved back, so the reference isn't lost. Cleared by
            choosing a model. */
        juce::String unresolvedModelId;

        /** Shown next to the part; for a pot or switch, also its control's name. */
        juce::String label;

        /** Potentiometer only: how the resistance follows the knob. */
        Taper taper = Taper::Linear;

        /** Pot and switch only: position in the strip, lower first; ties keep drawing order. */
        int controlOrder = 0;

        /** Capacitor only: electrolytic. Pin 0 is positive, it has ESR, and the build checks
            it isn't reverse-biased. */
        bool polarised = false;

        /** Switch only: its position at build time. Kept in step with switchPosition for
            changeovers (true on throw A). */
        bool closed = true;

        /** Changeover switch only: how it moves. */
        SwitchAction switchAction = SwitchAction::OnOn;

        /** Changeover switch only: lever position, 0 to positions - 1. The top position is
            throw A, so A is on the right in the strip: ON-ON is B, A; ON-OFF-ON is B, off, A;
            ON-ON-ON is C, B, A. */
        int switchPosition = 1;

        /** Two positions for ON-ON, three otherwise. */
        int getSwitchPositions() const noexcept
        {
            return switchAction == SwitchAction::OnOn ? 2 : 3;
        }

        /** Three throws for ON-ON-ON, two otherwise. */
        int getSwitchThrows() const noexcept
        {
            return switchAction == SwitchAction::OnOnOn ? 3 : 2;
        }

        /** The throw a position connects (0 for A), or -1 for an ON-OFF-ON's middle. Used by
            both the builder and the artwork. */
        int getThrowAt(int position) const noexcept
        {
            const int top = getSwitchPositions() - 1;
            position = juce::jlimit(0, top, position);

            if (switchAction == SwitchAction::OnOffOn)
                return position == 2 ? 0 : position == 0 ? 1 : -1;

            return top - position;
        }

        /** "SPDT" or "SP3T". */
        juce::String getSwitchKindName() const
        {
            return getSwitchThrows() == 3 ? "SP3T" : "SPDT";
        }

        /** Pin `index`'s offset: the common below, then the throws above, A on the left. */
        PinOffset getSwitchPinOffset(int index) const noexcept
        {
            if (index == 0)
                return {0, 2};

            const int t = index - 1;
            return {getSwitchThrows() == 3 ? 2 * t - 2 : 2 * t - 1, -2};
        }

        /** Pot only: knob position, 0 to 1. Seeded into the strip at build time and written
            back as the knob turns. */
        double controlPosition = 0.5;

        /** The lever position a 0-1 control value lands on, for a switch with `positions`
            positions: the nearest, halves going up, so 0.5 is the top of two. The one
            statement of it: the contacts, the drawing and the strip all ask here. */
        static int positionForValue(float value, int positions) noexcept
        {
            const int top = juce::jmax(1, positions - 1);
            const auto scaled = juce::jlimit(0.0f, 1.0f, value) * static_cast<float>(top);
            return juce::jlimit(0, top, static_cast<int>(std::floor(scaled + 0.5f)));
        }

        /** The part's control position, 0 to 1, for pots and switches alike. Non-controls
            read 0.5. */
        float getControlPosition() const noexcept
        {
            if (type == ElementType::Spdt)
                return static_cast<float>(juce::jlimit(0, getSwitchPositions() - 1, switchPosition))
                       / static_cast<float>(getSwitchPositions() - 1);

            if (isSwitch(type))
                return closed ? 1.0f : 0.0f;

            return type == ElementType::Potentiometer ? static_cast<float>(controlPosition) : 0.5f;
        }

        void setControlPosition(float position) noexcept
        {
            if (type == ElementType::Spdt)
            {
                switchPosition = positionForValue(position, getSwitchPositions());
                closed = switchPosition == getSwitchPositions() - 1;
            }
            else if (isSwitch(type))
                closed = positionForValue(position, 2) == 1;
            else if (type == ElementType::Potentiometer)
                controlPosition = juce::jlimit(0.0, 1.0, static_cast<double>(position));
        }

        /** Output only: whether the speaker cabinet is on, and its impulse response file. Not
            part of the circuit (no rebuild needed). The path is saved in the document. */
        bool cabEnabled = false;
        juce::String cabFile;

        /** Rectangle only: size in grid squares. Non-zero defaults so a new box is visible. */
        int width = 14, height = 10;

        /** Scope only: auto-scale the picture. Kept out of `value` so it never affects the
            build. */
        bool scopeAutoScale = true;

        /** Volts at the bottom and top of the picture when not auto-scaling. */
        double scopeMin = -5.0, scopeMax = 5.0;

        /** Time span of the picture, in seconds. */
        double scopeSeconds = 0.04;

        /** A point in the part's own frame, mirrored, rotated and moved onto the sheet. Used
            for pins and anything else in the part's frame. */
        juce::Point<int> localToSheet(int localX, int localY) const noexcept
        {
            // Mirror first, then rotate, so a flip swaps the part's own left and right.
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

        /** Pin `index` in grid coordinates, orientation applied. */
        juce::Point<int> getPinPosition(int index) const noexcept
        {
            const auto& info = getElementInfo(type);
            jassert(index >= 0 && index < getPinCount());

            const auto offset = type == ElementType::Spdt ? getSwitchPinOffset(index) : info.pins[index];
            return localToSheet(offset.x, offset.y);
        }

        /** Copies everything except identity and position. Add new members here. */
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
            target.switchAction = switchAction;
            target.switchPosition = switchPosition;
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

        int getPinCount() const noexcept
        {
            if (type == ElementType::Spdt)
                return 1 + getSwitchThrows();

            return getElementInfo(type).pinCount;
        }

        /** True for parts with a second value. */
        bool hasSecondValue() const noexcept
        {
            const auto* caption = getElementInfo(type).valueLabelB;
            return caption != nullptr && caption[0] != 0;
        }

        /** True when the second value is a rail (see ElementInfo::valueBIsRail). */
        bool isSecondValueRail() const noexcept { return getElementInfo(type).valueBIsRail; }

        /** A negative rail from what was typed: "15" and "-15" both mean 15 V below ground. */
        static double railBelowGround(double typed) noexcept { return -std::abs(typed); }

        /** Primary over secondary turns. Guarded against a zero secondary. */
        double getTurnsRatio() const noexcept { return valueB > 0.0 ? value / valueB : value; }

        /** True for parts with a typed value. */
        bool hasNumericValue() const noexcept { return getElementInfo(type).valueLabel[0] != 0; }

        /** True for parts with a model list. */
        bool hasModelChoice() const noexcept { return getElementInfo(type).models[0] != 0; }

        /** A Text note's display text: its label, or a placeholder when empty. */
        juce::String getDisplayText() const
        {
            if (type != ElementType::Text)
                return {};

            return label.isNotEmpty() ? label : "Text";
        }

        /** Half a Text note's width in grid squares (approximate). Used by both the renderer
            and the hit test. */
        int getTextHalfWidth() const
        {
            return juce::jmax(2, juce::roundToInt(0.32 * getDisplayText().length()) + 1);
        }

        // ======================================================================
        // The Node tag, drawn in code: its width follows its name.

        /** The tag's text: its name, or a red "?" when unnamed. */
        juce::String getNodeDisplayText() const
        {
            if (type != ElementType::Node)
                return {};

            return label.isNotEmpty() ? label : "?";
        }

        /** Width of the tag's body (without the point), in grid squares. Used by the renderer,
            hit test and bounds. */
        int getNodeBodyWidth() const
        {
            return juce::jmax(2, juce::roundToInt(0.62 * getNodeDisplayText().length()) + 1);
        }

        /** The tag in the part's own frame: a box with a one-square point on the right, its
            tip on the pin at {2, 0}. */
        juce::Rectangle<int> getNodeLocalBounds() const noexcept
        {
            const int body = getNodeBodyWidth();
            return {1 - body, -1, body + 1, 2};
        }

        /** The tag placed and rotated, on the sheet. */
        juce::Rectangle<int> getNodeBounds() const noexcept
        {
            const auto local = getNodeLocalBounds();

            const auto a = localToSheet(local.getX(), local.getY());
            const auto b = localToSheet(local.getRight(), local.getBottom());

            // Min/max by hand: rotation can put `b` above or left of `a`.
            const int left = juce::jmin(a.x, b.x), right = juce::jmax(a.x, b.x);
            const int top = juce::jmin(a.y, b.y), bottom = juce::jmax(a.y, b.y);

            return {left, top, right - left, bottom - top};
        }

        /** The grid area a Rectangle covers, clamped to the minimum size. Used by the renderer,
            hit test and Schematic::getElementBounds. */
        juce::Rectangle<int> getRectangleBounds() const noexcept
        {
            return juce::Rectangle<int>(0, 0, juce::jmax(minRectangleSize, width),
                                        juce::jmax(minRectangleSize, height))
                .withCentre({x, y});
        }
    };
} // namespace SchematicModel
