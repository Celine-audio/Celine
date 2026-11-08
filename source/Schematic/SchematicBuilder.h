#pragma once

#include <CelineEngine/Engine.h>
#include "Schematic.h"

#include <memory>

namespace SchematicModel
{
    //==========================================================================
    /** A control that stays live after the build: moving it re-stamps a value without a
        rebuild. */
    struct LiveControl
    {
        enum class Kind
        {
            Pot,
            Switch
        };

        Kind kind = Kind::Pot;

        /** Every drawn part this control works, in drawing order. More than one when parts
            share a label (ganged). */
        std::vector<int> elementIds;

        juce::String name;

        /** The first part; the control's name and starting position come from it. */
        int primaryElementId() const noexcept { return elementIds.empty() ? 0 : elementIds.front(); }

        /** Ganged pots: same label, one shaft. Each keeps its own value and taper. */
        std::vector<Circuit::Potentiometer> pots;

        /** One contact and the lever positions that close it. A plain switch is one contact
            closed in its upper position; a changeover has one per throw, closed where the
            lever puts the common on that throw. */
        struct Contact
        {
            Circuit::Switch contact;

            /** The part's position count, 2 or 3. */
            int positions = 2;

            /** Bit k set: closed in position k. */
            unsigned closedIn = 0;

            /** The position a 0-1 control value maps to. */
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

        /** Every contact this control works. Switches with the same label are ganged, plain
            switches and changeovers alike; unlabelled ones stay separate. */
        std::vector<Contact> contacts;

        /** 2 or 3: three if any switch on the control has three positions. */
        int positions = 2;

        /** The position that connects nothing (an ON-OFF-ON's middle), or -1. */
        int offPosition = -1;

        /** Position in the strip; ties keep drawing order. */
        int order = 0;

        /** Applies a 0-1 position to one circuit. */
        void apply(Circuit& circuit, float position) const
        {
            if (kind == Kind::Pot)
            {
                for (const auto& pot : pots)
                    pot.setPosition(circuit, position);

                return;
            }

            // Open before closing, so a changeover never makes two throws at once.
            for (const auto& contact : contacts)
                if (! contact.isClosedAt(position))
                    contact.contact.setClosed(circuit, false);

            for (const auto& contact : contacts)
                if (contact.isClosedAt(position))
                    contact.contact.setClosed(circuit, true);
        }
    };

    //==========================================================================
    /** The result of a build: one circuit per channel (same drawing, separate state), the
        live controls, probes and diagnostics. On failure, `error` says why and `circuits`
        is empty. */
    //==========================================================================
    /** A build message, with the part it concerns so the console can select it. */
    struct Diagnostic
    {
        enum class Severity
        {
            Error,    // the build stopped
            Warning,  // it built, but check this
            Info
        };

        Severity severity = Severity::Warning;

        /** The message, as a sentence. */
        juce::String text;

        /** The part's name ("R4", or the type for an unlabelled part); empty for the sheet as
            a whole. */
        juce::String subject;

        /** The part, or -1. */
        int elementId = -1;

        juce::Point<int> position;
        bool hasPosition = false;

        bool isError() const noexcept { return severity == Severity::Error; }
    };

    //==========================================================================
    /** A scope's two nets. Scopes stamp nothing; the processor reads the voltage across
        these. */
    struct ScopeProbe
    {
        int elementId = 0;
        juce::String positiveNode, referenceNode;

        /** Time span the picture covers. */
        double windowSeconds = 0.04;

        /** The same nodes resolved to indices, so the audio thread avoids name lookups. -1 if
            the circuit has no such node. */
        Circuit::NodeIndex positiveIndex = -1, referenceIndex = -1;
    };

    /** A part whose current follows from its terminal voltages alone: resistors (and the
        two halves of a pot). Valve currents can be read off their plate or cathode
        resistors. */
    struct CurrentProbe
    {
        int elementId = 0;
        juce::String nodeA, nodeB;

        /** Ohms between the two nodes. */
        double resistance = 0.0;

        /** The nodes resolved to indices (see ScopeProbe). */
        Circuit::NodeIndex indexA = -1, indexB = -1;
    };

    struct BuildResult
    {
        std::vector<std::unique_ptr<Circuit>> circuits;
        std::vector<LiveControl> controls;

        /** Every scope, in drawing order. */
        std::vector<ScopeProbe> probes;

        /** Every part whose current can be read. */
        std::vector<CurrentProbe> currentProbes;

        /** The blocking error, if any. Also in `diagnostics`. */
        juce::String error;

        /** Every message, errors included, in the order found. */
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

        /** Whether the build succeeded. Keyed on `error`, because the caller moves the
            circuits out. */
        bool isValid() const noexcept { return error.isEmpty(); }
    };

    //==========================================================================
    /** Build settings beyond the drawing. Changing one needs a rebuild. None may reduce
        accuracy. */
    struct BuildOptions
    {
        /** Use approximations of exp, log and pow (about 3e-10 relative error). Faster on
            some platforms, slightly slower on Apple Silicon. */
        bool fastMath = false;
    };

    //==========================================================================
    /** Turns a drawing into circuits. Pin meanings come from Element.h's table. Message
        thread: allocates and solves a DC operating point per channel. */
    BuildResult buildCircuits(const Schematic& schematic, double sampleRate, int channelCount,
                              BuildOptions options = {});
} // namespace SchematicModel
