#pragma once

#include "../Schematic/Schematic.h"
#include "SchematicSymbols.h"

#include <functional>
#include <optional>
#include <vector>

namespace SchematicUI
{
    //==========================================================================
    /**
        The sheet you draw on.

        Owns no circuit and knows nothing about audio -- it edits a Schematic and
        says when it changed. Whoever is listening decides whether that warrants
        a rebuild.

        The toolbar picks what a click does, and the keyboard does the same:

            Select (S)   click a part to select it, drag to move it. The
                         inspector edits whatever is selected.
            Wire (W)     drag out a wire; a diagonal drag corners into an L
            Delete (X)   click a part or a wire to remove it

        A bare letter names a part and arms it for placing -- R resistor,
        C capacitor, L inductor, T transistor, D diode, G ground, B box (the
        group rectangle, since R is already the resistor). Command turns a letter
        into an action on what is there: Cmd-R rotates, Cmd-F flips, either the
        part being placed or the selection.

        So the tools' letters -- S, W, X, plus F to frame the drawing -- are
        deliberately none of the parts' initials, and the two sets can grow
        without colliding. That is why Delete is X: D names the diode. F is a
        verb among nouns, which is allowed because it moves the *view* rather
        than the sheet.

        Dragging from empty sheet draws a selection box, and which way you drag
        decides what it catches: left to right takes only what fits inside,
        right to left takes anything it touches. The two are drawn differently --
        solid and dashed -- because otherwise the only way to know which you are
        doing is to let go and find out. Shift adds rather than replaces, and
        everything selected moves, rotates, flips and deletes together.

        Regardless of mode: Delete removes the selection, the wheel zooms about
        the cursor, right-drag pans, and middle-click picks a part up to clone --
        the Place tool is then armed with a copy of it, properties and all.

        A group box (ElementType::Rectangle) is the exception to "click a part to
        select it": it is grabbed by its edge, and everything inside stays
        clickable, since a box that swallowed the clicks aimed at the stage it
        rings would defeat the point of drawing one. Select it and its
        bottom-right corner becomes a resize handle.
    */
    class SchematicCanvas : public juce::Component
    {
       public:
        explicit SchematicCanvas(SchematicModel::Schematic& schematic);

        //======================================================================
        /** What a click does. */
        enum class Tool
        {
            Select,
            Wire,
            Delete,
            Place, // drops `pendingType` wherever you click
        };

        void setTool(Tool tool);
        Tool getTool() const noexcept { return tool; }

        /** Arms the Place tool with a part type, at that type's defaults. */
        void setPendingType(SchematicModel::ElementType type);

        /** Arms the Place tool with a *copy* of an existing part -- its value,
            model, label and settings, not just its type. Middle-click does this,
            so wanting five more of the resistor you already set up is one click
            to pick it up and five to put them down. */
        void cloneElement(int elementId);

        /** Fires when the tool changes for any reason, including the canvas
            dropping back to Select after placing a part -- so the toolbar can
            show which one is actually active. */
        std::function<void()> onToolChanged;

        /** Rotates a quarter turn: the part about to be placed when the Place
            tool is armed, otherwise the selection. Exposed so the inspector's
            button does exactly what R does.

            Rotating before placing matters more than it sounds -- most parts go
            down in the orientation you already know you want, and placing then
            rotating means every part lands wrong first. The angle is sticky
            across placements, so a run of horizontal resistors is set up once. */
        void rotate();
        void deleteSelection();

        /** Duplicates everything selected, a couple of squares down and right,
            and selects the copies -- so the next drag moves the new ones and
            the originals stay where they were. */
        void duplicateSelection();

        /** Mirrors about the part's own vertical axis: the part being placed
            when one is armed, otherwise the selection. Rotation can't do this --
            turning a triode twice puts its plate at the bottom, where flipping
            it moves the grid to the other side and leaves the plate up. */
        void flip();

        /** Mirrors about the part's own *horizontal* axis, which is the mirror
            `flip()` cannot do: a triode flipped this way has its plate at the
            bottom and its grid still on the left. Implemented as a mirror plus a
            half turn, which is exactly what reflecting in the other axis is. */
        void flipVertical();

        /** True while a click-drag is still in progress. Whoever is recording
            undo states uses this to keep one drag as one step rather than one
            per grid square crossed. */
        bool isMidGesture() const noexcept;

        /** Fires when a mouse gesture finishes, whether or not it changed
            anything. The boundary between "still dragging" and "done", which
            cannot be inferred from the change notifications alone. */
        std::function<void()> onGestureEnd;

        /** Undo and redo are the editor's business -- it owns the history and
            the buttons -- but the keyboard lives here, so the shortcuts are
            forwarded rather than reimplemented. */
        std::function<void()> onUndoRequested, onRedoRequested;

        /** What the Place tool is armed with, so the palette can light the right
            entry when a part was chosen by keyboard rather than by clicking. */
        SchematicModel::ElementType getPendingType() const noexcept { return pendingType; }

        //======================================================================
        /** Called whenever the drawing changed in a way that would change the
            circuit -- so the host can mark itself dirty, or rebuild. */
        std::function<void()> onSchematicChanged;

        /** Called when the selection changed, so an inspector can follow it. */
        std::function<void()> onSelectionChanged;

        /** The part the inspector edits: the first of the selection, or null.

            With several selected this is the one you started from, which is
            what an inspector full of one part's fields can honestly show. */
        SchematicModel::Element* getSelectedElement() noexcept;

        /** Every selected id, in the order they joined the selection. */
        const std::vector<int>& getSelection() const noexcept { return selectedIds; }

        /** The selected wires, alongside the parts. A separate list rather than
            one list of "things", because almost everything that acts on a
            selection acts on parts only -- rotate, flip, the inspector, the
            control strip -- and a merged list would have every one of them
            filtering it back out again. */
        const std::vector<int>& getSelectedWires() const noexcept { return selectedWireIds; }

        void setSelectedWires(std::vector<int> ids);

        /** Where a scope's picture comes from.

            A callback rather than a pointer to the processor: the canvas draws a
            schematic and knows nothing about audio, and the one thing it needs
            here is a snapshot of some numbers. Left null -- in the palette
            preview, in a test -- every scope simply draws as an empty screen. */
        ScopeReader scopeReader;

        /** Replaces the whole selection -- parts and wires together -- and says
            so once.

            Setting the two separately fires onSelectionChanged twice, and the
            first of those describes a selection that never really existed: the
            parts without the wires between them. That matters for an import,
            where the two halves are one block and the inspector would otherwise
            flicker through a state nobody asked for. */
        void setSelection(std::vector<int> elementIds, std::vector<int> wireIds);

        /** Adds a wire to the selection, or takes it out again. The wire
            equivalent of toggleInSelection. */
        void toggleWireInSelection(int wireId);

        /** Which part is under a *pixel* position, or -1.

            Lives here rather than on the Schematic because it needs the artwork
            to know how big anything is, and because where you can click is a
            question about how the sheet is drawn rather than about what the
            circuit is. Takes pixels, not grid squares: rounding to the nearest
            intersection first was most of what made the old hit test feel
            vague. */
        int elementAt(juce::Point<float> pixel) const noexcept;

        /** Which wire is under a pixel position, or -1. Parts win: a wire's end
            usually sits on a pin, and the part is what you meant. */
        int wireAt(juce::Point<float> pixel) const noexcept;

        /** Grid units to pixels, at whatever this view is currently zoomed and
            panned to. The inverse of what the mouse handlers do, and public
            because everything that aims at the sheet -- a test included -- has
            to speak the same coordinates the pointer does. */
        juce::Point<float> pixelAt(juce::Point<float> gridPoint) const noexcept;

        /** Where a grid point currently sits on screen. Anything that wants to
            point at the sheet -- or drive it -- needs the same mapping the
            drawing uses, rather than a second copy of it. */
        juce::Point<float> gridToPixel(juce::Point<int> gridPoint) const noexcept
        {
            return painter.toPixel(gridPoint);
        }

        bool isSelected(int elementId) const noexcept;

        /** The same question about a wire. Separate because the two lists are,
            and asked in the same places: a wire is picked up, dragged and put
            down by the gestures the parts use, so every rule that reads "is
            this already selected" has to be able to ask it of either. */
        bool isWireSelected(int wireId) const noexcept;

        /** Selects a part by id and brings it into view, for whoever names one
            without the user having clicked it -- the message console does. */
        void selectElement(int elementId);

        /** Re-reads the schematic after someone else changed it. */
        void refresh();

        /** Frames the whole drawing in the visible area. */
        void zoomToFit();

        //======================================================================
        void paint(juce::Graphics&) override;
        void resized() override;

        void mouseDown(const juce::MouseEvent&) override;
        void mouseDoubleClick(const juce::MouseEvent&) override;
        void mouseDrag(const juce::MouseEvent&) override;
        void mouseUp(const juce::MouseEvent&) override;
        void mouseMove(const juce::MouseEvent&) override;
        void mouseExit(const juce::MouseEvent&) override;
        void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
        bool keyPressed(const juce::KeyPress&) override;

        /** Height of the top ruler and width of the left one, in pixels. They
            differ because what has to fit differs: the top one stacks a row of
            numbers above its ticks, the left one puts its numbers beside them. */
        static constexpr int topRulerThickness = 33;
        static constexpr int leftRulerThickness = 29;

       private:
        //======================================================================
        /**
            One ruler, as its own opaque component.

            Opaque and separate for one reason: the cursor marker tracks the
            mouse, and repainting it must not repaint the drawing. A marker drawn
            inside SchematicCanvas::paint() costs a full canvas repaint every
            time it moves -- 0.5 ms on the default sheet, 3 ms on a busy one,
            against 0.1 ms for the strips alone. Opaque matters as much as
            separate: a transparent child makes JUCE repaint the parent behind
            it, putting the whole cost straight back.

            Takes no mouse input, so the canvas still gets everything.
        */
        class Ruler : public juce::Component
        {
           public:
            Ruler(const SchematicCanvas& o, bool horizontal);
            void paint(juce::Graphics&) override;

            /** Where to draw the cursor line, in this ruler's own pixels, or
                negative for "the mouse is elsewhere". */
            float cursor = -1.0f;

           private:
            const SchematicCanvas& owner;
            const bool isHorizontal;
        };

        Ruler topRuler{*this, true};
        Ruler leftRuler{*this, false};

        //======================================================================
        /** Opens the little editor over a part, for typing a value straight
            onto the sheet. The inspector can already do this; the two are not
            the same gesture, and travelling to a panel on the far side of the
            window to fix a number you are pointing at is friction. */
        void beginInlineEdit(SchematicModel::Element& element);
        void commitInlineEdit();
        void closeInlineEdit();

        /** Made when needed and destroyed when done, so nothing about it has to
            be maintained while no edit is happening. */
        std::unique_ptr<juce::TextEditor> inlineEditor;

        /** Which part it is editing, so a sheet that changes under it can be
            noticed rather than written to by id-that-no-longer-exists. */
        int inlineEditId = -1;

        /** Draws every scope's picture over its own symbol. */
        void drawScopeTraces(juce::Graphics&) const;

        /** Moves the markers and repaints the two strips -- and only them. */
        void updateRulerCursor(juce::Point<float> position);
        void notifyChanged();

        /** Replaces the selection with one part, or clears it with -1. */
        void setSelection(int elementId);

        /** Replaces the selection wholesale. */
        void setSelection(std::vector<int> ids);

        /** Adds if absent, removes if present -- what shift-click does. */
        void toggleInSelection(int elementId);

        /** Works out what the drawn box caught and selects it. */
        void applyBoxSelection(bool additive);

        /** The box being dragged, in this component's pixels. */
        juce::Rectangle<float> getSelectionBox() const noexcept;

        /** True while the box is being dragged leftwards, which in every CAD
            package means "catch anything you touch" rather than "catch what
            fits inside". */
        bool isCrossingBox() const noexcept { return boxCurrent.x < boxAnchor.x; }

        /** Line weight for a wire at the current zoom. */
        float wireThickness() const noexcept
        {
            return juce::jmax(1.0f, painter.gridSize * 0.13f);
        }

        void paintGrid(juce::Graphics& g) const;

        /** Boxes, then wires and junctions, then parts -- back to front. */
        void paintDrawing(juce::Graphics& g) const;

        /** Halos on the selection, and the handles it earns. */
        void paintSelection(juce::Graphics& g) const;

        /** The wire being dragged, the selection box, the ghost of the part
            about to be placed. */
        void paintInFlight(juce::Graphics& g) const;

        /** The Command-modified shortcuts: undo, redo, duplicate, rotate, flip. */
        bool commandKeyPressed(const juce::KeyPress& key);

        /** Drops the armed part where it was clicked. */
        void placePendingElement(juce::Point<int> grid);

        /** The Select tool's mouse-down: handles, then parts, then wires, then
            the selection box. */
        void beginSelectGesture(const juce::MouseEvent& event, juce::Point<int> grid);

        /** Starts a corner or wire-end drag if the click landed on one. */
        bool grabResizeHandle(const juce::MouseEvent& event, juce::Point<int> grid);

        /** Arms the move-drag that mouseDrag carries out. Shared, because a
            part and a wire are picked up the same way and forgetting one of the
            three fields is a drag that jumps or never starts. */
        void beginDrag(juce::Point<int> grid);

        bool deleteAt(juce::Point<float> pixel);

        /** Whether this grid point is on the selected group box's resize corner. */
        bool isNearResizeHandle(const SchematicModel::Element& element,
                                juce::Point<int> grid) const noexcept;

        /** The wire whose ends wear resize handles, or -1: one wire, selected
            on its own. With anything else selected a drag moves the whole lot,
            so a handle would advertise a gesture that is not on offer. Asked by
            the painter and the mouse alike, so what is drawn and what is
            grabbable cannot disagree. */
        int wireWithHandles() const noexcept;

        /** Half-diagonal of a wire end's drag handle, in pixels. One number so
            the diamond that is drawn and the area that answers a click cannot
            drift apart -- they did, and the gap was most of a grid square. */
        float wireHandleReach() const noexcept;

        /** Which end of `wire` this *pixel* is on: 0 for a, 1 for b, -1 for
            neither. Pixels rather than grid squares, because the handle is a
            fixed shape on screen and a snapped grid point cannot tell the
            difference between its middle and its edge. */
        int wireHandleAt(const SchematicModel::Wire& wire, juce::Point<float> pixel) const noexcept;

        /** Joins up any collinear wires the last gesture left touching, keeping
            `preferredId` alive, and prunes selected ids that no longer exist. */
        void mergeWiresAfterEdit(int preferredId);

        /** Drags a group box's bottom-right corner to a grid point, leaving the
            other three where they were. */
        void resizeTo(SchematicModel::Element& element, juce::Point<int> grid);

        juce::Point<int> gridAt(juce::Point<float> pixel) const noexcept { return painter.toGrid(pixel); }

        SchematicModel::Schematic& schematic;
        SymbolPainter painter;

        Tool tool = Tool::Select;
        /** Set when the Place tool is cloning: the part middle-click picked up.
            Its properties go onto every part placed until something else is
            armed, which is what makes "one more of those" a click. */
        std::optional<SchematicModel::Element> pendingClone;

        SchematicModel::ElementType pendingType = SchematicModel::ElementType::Resistor;
        int pendingOrientation = 0;
        bool pendingMirrored = false;

        /** Everything selected, oldest first. One id is the common case and a
            vector of one costs nothing; the alternative was an id plus a set
            that had to agree with it. */
        std::vector<int> selectedIds;
        std::vector<int> selectedWireIds;

        // Drag state.
        bool draggingElement = false;
        bool resizingElement = false;

        /** Which end of the selected wire is being dragged, or -1. */
        int resizingWireEnd = -1;

        /** Where the drag was last seen, so a move can be applied to every
            selected part as a delta. An offset from one part's centre only
            works when there is one part. */
        juce::Point<int> dragLastGrid;

        /** Set once a drag actually moves something, so that clicking a part
            that is already selected can keep the selection while a *click*
            without a drag narrows it down to just that part. */
        bool dragMovedSomething = false;

        bool boxSelecting = false;
        juce::Point<float> boxAnchor, boxCurrent;
        bool draggingWire = false;
        juce::Point<int> wireStart;
        juce::Point<int> wireEnd;
        bool panning = false;
        juce::Point<float> panOrigin;

        juce::Point<int> hoverGrid;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SchematicCanvas)
    };
} // namespace SchematicUI
