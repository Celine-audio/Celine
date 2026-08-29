#pragma once

#include "Element.h"

#include <vector>

namespace SchematicModel
{
    //==========================================================================
    /** A drawn connection. Always axis-aligned: a diagonal wire has no
        meaningful "does this pin touch it" test, and every schematic editor
        worth using draws in right angles anyway. */
    struct Wire
    {
        /** Identity, for the same reason an Element has one: a wire can be
            selected, and a selection has to survive its neighbours being
            deleted. An index into the vector would not -- removing one wire
            renumbers every wire after it. */
        int id = 0;

        juce::Point<int> a, b;

        bool isVertical() const noexcept { return a.x == b.x; }
        bool isHorizontal() const noexcept { return a.y == b.y; }

        /** True if `p` lies anywhere along this wire, endpoints included. This
            is what makes a T-junction work: a pin or wire end landing partway
            along another wire is connected to it, with no explicit junction
            needed. */
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
    /**
        The result of working out what is connected to what.

        `netOfPin` is indexed the same way the schematic's elements are: for
        element `e` and pin `p`, `netOfPin[e][p]` is a net index, and
        `netNames[index]` is the node name the builder will hand to Circuit.

        Two nets may legitimately share a name. That is not a bug and it is how
        ground works: every Ground symbol names its net "gnd", so dropping
        ground symbols around a sheet connects them without drawing a wire
        across the whole drawing. Circuit maps nodes by name, so identically
        named nets become one node when the circuit is built.
    */
    struct NetList
    {
        std::vector<std::array<int, maxPinsPerElement>> netOfPin;
        std::vector<juce::String> netNames;

        /** Nets with only one thing attached. Almost always a mistake -- a part
            left dangling, or a wire that stops one grid square short -- so the
            editor can point at them. */
        std::vector<int> danglingNets;
    };

    //==========================================================================
    /**
        A schematic: some elements, some wires, and the ability to say which
        nets they form.

        Deliberately just a document. It never builds a Circuit, never touches
        audio, and has no idea one exists -- SchematicBuilder does that, one way,
        in one place. Everything here runs on the message thread.
    */
    class Schematic
    {
       public:
        Schematic() = default;

        //======================================================================
        // Elements

        /** Places a new element at a grid position and returns its id. */
        int addElement(ElementType type, int x, int y);

        void removeElement(int id);

        Element* findElement(int id) noexcept;
        const Element* findElement(int id) const noexcept;

        const std::vector<Element>& getElements() const noexcept { return elements; }
        std::vector<Element>& getElements() noexcept { return elements; }

        /** The element whose body covers this grid point, or -1. Searched
            newest first, so a part placed on top of another is the one you
            grab. */
        int findElementAt(juce::Point<int> gridPoint) const noexcept;

        /** The grid rectangle an element's symbol occupies, used for hit
            testing and for framing the view. */
        juce::Rectangle<int> getElementBounds(const Element& element) const noexcept;

        /** Whether a click at this grid point lands on the element. Usually the
            bounds, but a Rectangle is grabbed by its edge rather than by the
            area it encloses -- see the definition. */
        bool hitTest(const Element& element, juce::Point<int> gridPoint) const noexcept;

        //======================================================================
        // Wires

        /** Adds a wire, splitting a diagonal drag into an L of two segments --
            Wire::contains has no meaningful test for a diagonal, so a diagonal
            wire would conduct nothing. Zero-length drags add nothing. */
        void addWire(juce::Point<int> from, juce::Point<int> to);

        /** The wire with this id, or null. */
        const Wire* findWire(int id) const noexcept;

        /** Removes it, if it is there. True if anything went. */
        bool removeWire(int id);

        /** Moves both ends by the same offset. */
        void moveWire(int id, juce::Point<int> delta);

        /** Drags one end of a wire along the wire's own axis, leaving the other
            where it is. `end` is 0 for `a` and 1 for `b`.

            Along the axis and not wherever the pointer went, because a wire is
            axis-aligned by definition -- `contains()` has no answer for a
            diagonal, so a free drag would have to either refuse most positions
            or silently re-corner the wire into an L. Sliding the end is what
            "resize" means here, and it is the only reading that always works.

            The end may pass through the other one and come out the far side --
            that is what dragging it there means. The single refusal is landing
            exactly *on* the other end: a zero-length wire is invisible,
            unclickable and impossible to get rid of except by deleting whatever
            it is sitting on. Returns true if anything actually moved. */
        bool resizeWireEnd(int id, int end, juce::Point<int> gridPoint);

        /** Joins wires that lie on the same line and touch, so two segments
            drawn end to end become the one wire they look like -- two squares
            plus four squares is one six-square wire.

            Runs over the whole sheet rather than the pair just edited, because
            one join can create another: closing a gap between A and B can leave
            the result touching C, and stopping after the first pass would leave
            a sheet that is *nearly* merged and depends on drawing order.

            Electrically this changes nothing, which is what makes it safe to do
            behind the user's back. A pin or a third wire meeting the old seam
            still connects: `Wire::contains` is what net extraction asks, and the
            merged wire runs through the seam exactly as the two halves did.
            Junction dots survive for the same reason -- findJunctions() already
            draws one where a wire *end* sits strictly inside another wire, which
            is precisely what the seam becomes.

            `preferredId` keeps that wire's identity when it is one of a merged
            pair, so the wire you are dragging is still the wire you have
            selected when you let go. Returns how many wires went. */
        int mergeCollinearWires(int preferredId = -1);

        /** Removes any wire passing through this point. Returns how many went. */
        int removeWiresAt(juce::Point<int> gridPoint);

        const std::vector<Wire>& getWires() const noexcept { return wires; }

        /** Points where three or more wire ends meet, so the renderer can draw
            a junction dot. Two collinear ends meeting need no dot. */
        std::vector<juce::Point<int>> findJunctions() const;

        //======================================================================
        // Connectivity

        /** Works out the nets. O(points * wires), which at schematic sizes is
            nothing, and avoids any need for the drawing to maintain
            connectivity incrementally as it is edited. */
        NetList extractNets() const;

        //======================================================================
        // Whole-sheet operations

        void clear();
        bool isEmpty() const noexcept { return elements.empty() && wires.empty(); }

        /** Every element of a given type, for the checks the builder needs to
            make (exactly one input, at least one ground, and so on). */
        int countElementsOfType(ElementType type) const noexcept;

        /** The grid rectangle everything drawn fits inside, or an empty one for
            an empty sheet.

            Built with a `first` flag rather than by unioning onto a default
            rectangle, for the reason getElementBounds is written out by hand:
            JUCE calls a zero-sized rectangle empty and getUnion returns the
            *other* operand when either side is empty, so a sheet whose first
            item happens to be a single point would collapse the answer to that
            point. */
        juce::Rectangle<int> getContentBounds() const noexcept;

        /** What an import brought with it, so the caller can select it.

            Which is the whole point of importing: the first thing anyone does
            with a block dropped onto a sheet is move it somewhere, and hunting
            for it in order to select it first would be the worst part of the
            feature. */
        struct Merged
        {
            std::vector<int> elementIds;
            std::vector<int> wireIds;
        };

        /** Adds a copy of everything in `other`, moved by `delta` grid squares.

            Ids are reassigned rather than kept. Both sheets number from one, so
            a merge that preserved them would leave two parts that findElement()
            cannot tell apart -- and the second of them unreachable, since every
            lookup here returns the first match. */
        Merged merge(const Schematic& other, juce::Point<int> delta);

        //======================================================================
        // Persistence

        juce::ValueTree toValueTree() const;

        /** What restoreFromValueTree() had to work around on the way in, as
            sentences ready for the console.

            Models are named in a saved sheet, and a name this build does not
            have is a real possibility -- a file from a later version, or one
            from before the model was renamed. The load cannot refuse (the rest
            of the sheet is fine) and must not stay quiet (the sheet would build
            with the wrong parts), so it substitutes model 0 and leaves the
            reason here. PluginProcessor::rebuild() drains these into the build
            result, which is what puts them in front of anyone.

            Cleared by the next restoreFromValueTree(), and by takeLoadWarnings(). */
        const juce::StringArray& getLoadWarnings() const noexcept { return loadWarnings; }

        /** Adds one, for a caller that noticed something about the load the
            drawing itself could not.

            The document wrapping this sheet knows things the sheet does not --
            which build wrote it, most of all -- and its findings belong in the
            same place, because they reach the console by the same route and are
            about the same event. */
        void addLoadWarning(juce::String text) { loadWarnings.add(std::move(text)); }

        /** The same, but empties the list. Fired once per load rather than once
            per rebuild: after the first report the substitution has either been
            corrected or accepted, and repeating it on every Rebuild would be
            describing a sheet that no longer exists. */
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
