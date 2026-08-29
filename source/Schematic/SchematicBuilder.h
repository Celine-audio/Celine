#pragma once

#include "../CelineEngine/Engine.h"
#include "Schematic.h"

#include <memory>

namespace SchematicModel
{
    //==========================================================================
    /**
        A control the user can still move after the circuit has been built.

        The whole reason the rebuild/live split exists: changing a pot is a
        re-stamp and re-factorisation the engine already does per block without
        allocating, so a knob stays live while the topology stays frozen until
        the next explicit rebuild.
    */
    struct LiveControl
    {
        enum class Kind
        {
            Pot,
            Switch
        };

        Kind kind = Kind::Pot;

        /** Every drawn part this one control works, in the order they were
            found. Usually one; more when parts share a label and gang. */
        std::vector<int> elementIds;

        juce::String name;

        /** The part the control is named after, and the one its position is
            read from when the strip is seeded. Ganged parts are the same
            control, so any of them would do -- the first is simply the one
            that was drawn first. */
        int primaryElementId() const noexcept { return elementIds.empty() ? 0 : elementIds.front(); }

        /** Ganged pots: a dual-gang volume is one shaft turning two tracks, and
            two drawn pots sharing a label are how you say so. Same rule as the
            switches below, for the same reason -- the label is the only thing
            on the drawing that can say two parts are one control. */
        std::vector<Circuit::Potentiometer> pots;

        /** Every contact this one control works, single-throw and double.

            Switches sharing a label are *ganged* into one control and throw
            together, which is how an amp's channel switch moves four things at
            once. The two lists gang with each other freely, since a real
            multi-pole switch mixes make-break and changeover contacts.

            Keyed on the label because that is the only thing on the drawing that
            says two switches are one -- an unlabelled one is named by ordinal
            and stays on its own.

            A changeover is two Switches held in opposition, and a type of its
            own in the engine so that "never both" cannot be broken by
            accident. */
        std::vector<Circuit::Switch> toggles;
        std::vector<Circuit::Changeover> changeovers;

        /** Where this control sits in the strip. Lower comes first; equal
            values keep the order they were drawn in. */
        int order = 0;

        /** Applies a 0-to-1 position to one built circuit. For a switch,
            anything at or above halfway is closed. */
        void apply(Circuit& circuit, float position) const
        {
            if (kind == Kind::Pot)
            {
                for (const auto& pot : pots)
                    pot.setPosition(circuit, position);

                return;
            }

            const bool on = position >= 0.5f;

            for (const auto& toggle : toggles)
                toggle.setClosed(circuit, on);

            // On means throw A, matching the drawn symbol's "Throw A (on)".
            for (const auto& changeover : changeovers)
                changeover.select(circuit, ! on);
        }
    };

    //==========================================================================
    /**
        What came out of trying to build a schematic.

        `circuits` holds one per channel, built from the same drawing in the same
        order -- so one list of controls addresses all of them -- but each keeps
        its own capacitor and device state, which is what makes stereo stereo.

        A failed build reports why in `error` and leaves `circuits` empty. That
        text is written for a person: "no ground symbol", not "singular matrix".
    */
    //==========================================================================
    /**
        One thing the build has to say, and where on the sheet to look for it.

        Six problems in one sentence is a paragraph nobody reads to the end of;
        six rows each naming one part is a list you work through. So every
        message carries the part it is about, which lets the console name it,
        place it, and select it when clicked.
    */
    struct Diagnostic
    {
        enum class Severity
        {
            Error,    // the build stopped
            Warning,  // it built, but check this
            Info
        };

        Severity severity = Severity::Warning;

        /** What is wrong, as a sentence. */
        juce::String text;

        /** What it is about -- "R4", or "Capacitor" for an unlabelled part.
            Empty when the message is about the sheet as a whole. */
        juce::String subject;

        /** The part, so the console can select it. -1 when there isn't one. */
        int elementId = -1;

        juce::Point<int> position;
        bool hasPosition = false;

        bool isError() const noexcept { return severity == Severity::Error; }
    };

    //==========================================================================
    /** Where one scope element ended up hanging, as a pair of node names.

        The scope stamps nothing, so this is the only trace it leaves in a build:
        the two nets its pins landed on, which is enough for the processor to
        read a voltage across them after each sample without the engine having to
        know a scope exists at all. */
    struct ScopeProbe
    {
        int elementId = 0;
        juce::String positiveNode, referenceNode;

        /** How wide a slice of time this scope's picture covers. Per probe
            rather than one setting for the sheet, so a sheet can watch a 20 kHz
            ripple on one and an envelope settling on another. */
        double windowSeconds = 0.04;

        /** The same two nodes, resolved against the built circuit.

            The names are what the drawing produces and what a diagnostic has to
            print; these are what the audio thread reads. Looking a name up is a
            string hash and a map probe, and a scope reads two nodes every
            sample, so resolving them once per build is the difference between
            two array loads and the better part of a million hashes a second.
            Filled by buildCircuits(); -1 if the circuit has no such node. */
        Circuit::NodeIndex positiveIndex = -1, referenceIndex = -1;
    };

    /** A part whose current can be read back off the solved node voltages.

        Only parts whose current is a function of their terminal voltages *and
        nothing else* belong here -- which today means resistors and the two
        halves of a potentiometer. A capacitor's current depends on the rate of
        change of its voltage and a valve's on its own internal state, so both
        would need the engine to hand out something it does not currently keep,
        and a number that is only nearly right is worse here than no number:
        the whole use of this readout is deciding whether a part is inside its
        rating.

        In practice the restriction costs less than it sounds. A valve's plate
        and cathode currents are exactly the currents through its plate and
        cathode resistors, so they are already readable by clicking the resistor
        next to it. */
    struct CurrentProbe
    {
        int elementId = 0;
        juce::String nodeA, nodeB;

        /** Ohms across those two nodes. The current is the voltage between them
            divided by this, so a second law would need a second field. */
        double resistance = 0.0;

        /** Those two nodes, resolved against the built circuit. See the same
            fields on ScopeProbe for why the names alone are not enough. */
        Circuit::NodeIndex indexA = -1, indexB = -1;
    };

    struct BuildResult
    {
        std::vector<std::unique_ptr<Circuit>> circuits;
        std::vector<LiveControl> controls;

        /** Every scope on the sheet, in the order they were drawn. */
        std::vector<ScopeProbe> probes;

        /** Every part whose current can be read. See CurrentProbe. */
        std::vector<CurrentProbe> currentProbes;

        /** The one blocking reason, when there is one. Kept as its own field
            because `isValid()` keys on it and because a failed build has
            exactly one headline; the same message also appears in
            `diagnostics`, which is the full list. */
        juce::String error;

        /** Everything the build has to say, errors included, in the order it
            found them. */
        std::vector<Diagnostic> diagnostics;

        void add(Diagnostic::Severity severity, juce::String text,
                 juce::String subject = {}, const Element* element = nullptr)
        {
            Diagnostic d;
            d.severity = severity;
            d.text = std::move(text);
            d.subject = std::move(subject);

            if (element != nullptr)
            {
                d.elementId = element->id;
                d.position = {element->x, element->y};
                d.hasPosition = true;

                if (d.subject.isEmpty())
                    d.subject = element->label.isNotEmpty()
                                  ? element->label
                                  : juce::String(getElementInfo(element->type).name);
            }

            diagnostics.push_back(std::move(d));
        }

        /** Whether the build succeeded. Deliberately keyed on `error` rather
            than on `circuits` being populated, because the caller moves the
            circuits out to install them and would otherwise see its own
            successful build turn into a failure the moment it took delivery. */
        bool isValid() const noexcept { return error.isEmpty(); }
    };

    //==========================================================================
    /**
        Deliberate inaccuracies, traded for CPU.

        Everything here defaults to the accurate answer, so a caller that says
        nothing gets the full simulation. These are build-time rather than live:
        each one changes what the matrix contains, so changing one costs a
        rebuild and a fresh bias point.
    */
    struct BuildOptions
    {
        /** Whether valves wire in their own interelectrode capacitance. The
            grid-plate one is the Miller path, which rolls off the top of a stage
            driven from a high source impedance.

            Three capacitors per valve, and in DK every capacitor is a state
            variable. Measured at **+23% CPU** on a six-triode channel: the most
            expensive thing the simulation does that a player might reasonably
            choose to go without. */
        bool interelectrodeCapacitance = true;

        /** Whether transistors wire in their junction capacitances (CJE/CJC).
            Miller multiplies the base-collector one by the stage's gain, which
            is what darkens a high-gain stage driven through a large resistance.

            On by default like the rest: the CPU is opt-out rather than the
            physics opt-in. A sheet drawn before this existed does sound slightly
            darker now, which is the right answer -- the circuit has this
            capacitance in it.

            Two DK state variables per transistor: **+12% CPU** on a
            two-transistor fuzz, +3-4% on a booster. */
        bool transistorJunctionCapacitance = true;

        /** Whether the transistor Early effect is modelled -- an output
            resistance ro = VAF/Ic across the collector load, trimming a stage's
            gain by a few percent.

            Costs nothing measurable: two multiplies inside a Newton step that
            was happening anyway, no new state variable. Only models whose cards
            carry a VAF figure respond; the rest read as an infinite Early
            voltage, which is what they did before this existed. */
        bool transistorEarlyEffect = true;

        /** Whether the device models use approximations of exp, log and pow.

            Accurate to about 3e-10 relative, four orders tighter than the Newton
            tolerance they feed. Whether it is *faster* is a platform question:
            measured a wash on Apple Silicon, whose libm is already excellent.
            Hence a switch rather than a decision baked in. */
        bool fastMath = false;

        /** Whether Newton starts each sample from a straight-line extrapolation
            of the last two rather than from the last one alone.

            Not an inaccuracy: Newton reaches the same root either way and the
            output is bit-identical. Purely a guess at where the answer will be,
            and whether it pays depends on the circuit -- **-7% to -12%** on
            pedals, but **+11%** on a six-triode high-gain preamp, where the port
            trajectories are clipped rather than smooth and the guess triggers
            the voltage limiters. Off by default for that reason. */
        bool predictNewtonSeed = false;
    };

    //==========================================================================
    /**
        Turns a drawing into circuits.

        The one place geometry becomes a netlist. Everything it needs to know
        about which pin of a part means what lives in Element.h's table, so
        adding a part type is a table entry plus a case here.

        Runs on the message thread: it allocates, and it solves a DC operating
        point per channel.
    */
    BuildResult buildCircuits(const Schematic& schematic, double sampleRate, int channelCount,
                              BuildOptions options = {});
} // namespace SchematicModel
