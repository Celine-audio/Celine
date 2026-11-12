#pragma once

#include "Element.h"

#include <vector>

namespace SchematicModel
{
    //==========================================================================
    /** A drawn connection. Always horizontal or vertical. */
    struct Wire
    {
        /** Stable id, so a selection survives other wires being removed. */
        int id = 0;

        juce::Point<int> a, b;

        bool isVertical() const noexcept { return a.x == b.x; }
        bool isHorizontal() const noexcept { return a.y == b.y; }

        /** True if `p` is anywhere on the wire, ends included. A pin or wire end landing
            partway along a wire connects to it. */
        bool contains(juce::Point<int> p) const noexcept
        {
            if (isVertical())
                return p.x == a.x && p.y >= juce::jmin(a.y, b.y) && p.y <= juce::jmax(a.y, b.y);

            if (isHorizontal())
                return p.y == a.y && p.x >= juce::jmin(a.x, b.x) && p.x <= juce::jmax(a.x, b.x);

            return false;
        }
    };

    //==========================================================================
    /** Which net each pin is on: `netOfPin[e][p]` indexes `netNames`, the node names
        passed to Circuit. Nets may share a name (every Ground is "gnd"); Circuit merges
        them by name. */
    struct NetList
    {
        std::vector<std::array<int, maxPinsPerElement>> netOfPin;
        std::vector<juce::String> netNames;

        /** Nets with only one connection, usually a mistake. */
        std::vector<int> danglingNets;
    };

    //==========================================================================
    /** The drawing: elements and wires, and the nets they form. Never builds a circuit
        (SchematicBuilder does). Message thread only. */
    class Schematic
    {
       public:
        Schematic() = default;

        // ======================================================================
        // Elements

        /** Places a new element at a grid position and returns its id. */
        int addElement(ElementType type, int x, int y);

        void removeElement(int id);

        Element* findElement(int id) noexcept;
        const Element* findElement(int id) const noexcept;

        const std::vector<Element>& getElements() const noexcept { return elements; }
        std::vector<Element>& getElements() noexcept { return elements; }

        /** The element covering this point, or -1. Newest first. */
        int findElementAt(juce::Point<int> gridPoint) const noexcept;

        /** The grid rectangle an element's symbol covers. */
        juce::Rectangle<int> getElementBounds(const Element& element) const noexcept;

        /** Whether a click here lands on the element. A Rectangle is grabbed by its edge. */
        bool hitTest(const Element& element, juce::Point<int> gridPoint) const noexcept;

        // ======================================================================
        // Wires

        /** Adds a wire; a diagonal is split into two segments. Zero-length adds nothing. */
        void addWire(juce::Point<int> from, juce::Point<int> to);

        /** The wire with this id, or null. */
        const Wire* findWire(int id) const noexcept;

        /** Removes the wire. True if it existed. */
        bool removeWire(int id);

        /** Moves both ends by the same offset. */
        void moveWire(int id, juce::Point<int> delta);

        /** Moves one end of a wire (`end` 0 for `a`, 1 for `b`) along the wire's axis. The end
            may pass the other one but not land on it. True if it moved. */
        bool resizeWireEnd(int id, int end, juce::Point<int> gridPoint);

        /** Joins touching wires on the same line into one, repeating until nothing changes.
            Connectivity is unchanged. `preferredId` keeps its id if merged. Returns how many
            wires were removed. */
        int mergeCollinearWires(int preferredId = -1);

        /** Removes every wire through this point. Returns how many. */
        int removeWiresAt(juce::Point<int> gridPoint);

        const std::vector<Wire>& getWires() const noexcept { return wires; }

        /** Points needing a junction dot: three or more wire ends meeting. */
        std::vector<juce::Point<int>> findJunctions() const;

        // ======================================================================
        // Connectivity

        /** Computes the nets from scratch. O(points * wires). */
        NetList extractNets() const;

        // ======================================================================
        // Whole-sheet operations

        void clear();
        bool isEmpty() const noexcept { return elements.empty() && wires.empty(); }

        /** Number of elements of a type. */
        int countElementsOfType(ElementType type) const noexcept;

        /** The grid rectangle everything fits in, or empty for an empty sheet. */
        juce::Rectangle<int> getContentBounds() const noexcept;

        /** What an import added, so it can be selected. */
        struct Merged
        {
            std::vector<int> elementIds;
            std::vector<int> wireIds;
        };

        /** Adds a copy of `other`, offset by `delta`. Ids are reassigned. */
        Merged merge(const Schematic& other, juce::Point<int> delta);

        // ======================================================================
        // Persistence

        juce::ValueTree toValueTree() const;

        /** Problems restoreFromValueTree() worked around (e.g. an unknown model replaced by
            model 0), as console messages. Cleared on the next restore. */
        const juce::StringArray& getLoadWarnings() const noexcept { return loadWarnings; }

        /** Adds a load warning found by the caller. */
        void addLoadWarning(juce::String text) { loadWarnings.add(std::move(text)); }

        /** Returns the load warnings and clears them, so each is reported once. */
        juce::StringArray takeLoadWarnings()
        {
            auto taken = std::move(loadWarnings);
            loadWarnings.clear();
            return taken;
        }
        void restoreFromValueTree(const juce::ValueTree& tree);

       private:
        std::vector<Element> elements;
        std::vector<Wire> wires;
        int nextWireId = 1;
        int nextElementId = 1;

        /** Filled by restoreFromValueTree(), drained by takeLoadWarnings(). */
        juce::StringArray loadWarnings;
    };
} // namespace SchematicModel
