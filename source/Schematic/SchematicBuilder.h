#pragma once

#include <CelineEngine/Engine.h>
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

        /** One make-break contact, and the lever positions that close it.

            Every switch is built from these: a plain switch is one contact
            closed in its upper position; a changeover is one per throw per
            pole, each closed in the positions that put the common on it. No
            two contacts of one pole share a position, which is what keeps a
            changeover from ever making both throws at once. */
        struct Contact
        {
            Circuit::Switch contact;

            /** Its part's position count, 2 or 3. */
            int positions = 2;

            /** Bit k set: closed in position k. */
            unsigned closedIn = 0;

            /** The position a 0-to-1 control value lands on. */
            static int positionFor(float value, int positions) noexcept
            {
                const int top = positions - 1;
                return juce::jlimit(0, top, juce::roundToInt(juce::jlimit(0.0f, 1.0f, value) * static_cast<float>(top)));
            }

            bool isClosedAt(float value) const noexcept
            {
                return ((closedIn >> positionFor(value, positions)) & 1u) != 0;
            }
        };

        /** Every contact this one control works, of every switch on it.

            Switches sharing a label are *ganged* into one control and throw
            together, which is how an amp's channel switch moves four things at
            once. Plain switches and changeovers of any shape gang freely, since
            a real multi-pole switch mixes make-break and changeover contacts.

            Keyed on the label because that is the only thing on the drawing that
            says two switches are one -- an unlabelled one is named by ordinal
            and stays on its own. */
        std::vector<Contact> contacts;

        /** How many positions the control has: three if any switch on it has
            three, else two. The strip draws a toggle for two and a three-way
            lever for three. */
        int positions = 2;

        /** The position that connects nothing -- the middle of an ON-OFF-ON --
            or -1. The strip shows it unlit, as it does a plain switch's off. */
        int offPosition = -1;

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

            // Opened before any is closed, so moving a changeover never makes
            // both throws even for the length of this loop.
            for (const auto& contact : contacts)
                if (! contact.isClosedAt(position))
                    contact.contact.setClosed(circuit, false);

            for (const auto& contact : contacts)
                if (contact.isClosedAt(position))
                    contact.contact.setClosed(circuit, true);
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
        How a circuit is built, beyond what the drawing says.

        Build-time rather than live, because each one changes what the matrix
        contains or how it is solved, so changing one costs a rebuild and a
        fresh bias point. Nothing here may make the simulation less accurate:
        the physics is not optional, and switches that traded it for CPU were
        measured to buy almost nothing and removed.
    */
    struct BuildOptions
    {
        /** Whether the device models use approximations of exp, log and pow.

            Accurate to about 3e-10 relative, four orders tighter than the Newton
            tolerance they feed. Whether it is *faster* is a platform question:
            0.3-2.8% slower on Apple Silicon, whose libm is excellent, but
            Windows' and Linux's runtimes have historically been slower. Hence a
            switch rather than a decision baked in. */
        bool fastMath = false;
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
