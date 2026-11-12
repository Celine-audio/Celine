#pragma once

#include "../Schematic/Schematic.h"
#include "CaptionObstacles.h"
#include "SchematicSymbols.h"

#include <functional>
#include <optional>
#include <vector>

namespace Celine
{
    //==========================================================================
    /** The sheet you draw on. Edits a Schematic and reports changes; knows nothing about
        audio.

        Tools: Select (S) clicks and drags parts, Wire (W) drags out wires (a diagonal
        becomes an L), Delete (X) removes what is clicked. A bare letter arms a part for
        placing (R, C, L, T, D, G, I, O, B for box); Cmd-R rotates and Cmd-F flips the part
        being placed or the selection. F frames the drawing.

        Dragging on empty sheet draws a selection box: left to right selects what is fully
        inside, right to left anything it touches. Shift adds to the selection.

        Always: Delete removes the selection, the wheel zooms about the cursor, right-drag
        pans, middle-click arms the Place tool with a copy of the part under the cursor.

        A group box is selected by its edge, so the parts inside stay clickable; selected,
        its bottom-right corner resizes it. */
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

        /** Arms the Place tool with a part type, at its defaults. */
        void setPendingType(SchematicModel::ElementType type);

        /** Arms the Place tool with a copy of an existing part (value, model, label, settings). */
        void cloneElement(int elementId);

        /** Fires on any tool change, including the automatic return to Select. */
        std::function<void()> onToolChanged;

        /** Rotates a quarter turn: the part being placed, or else the selection. The angle
            sticks across placements. */
        void rotate();
        void deleteSelection();

        /** Duplicates the selected parts and wires, offset down-right, and selects the copies. */
        void duplicateSelection();

        /** Mirrors left-right about the part's own axis: the part being placed, or else the
            selection. */
        void flip();

        /** Mirrors top-bottom about the part's own axis (a mirror plus a half turn). */
        void flipVertical();

        /** True while a mouse drag is in progress. */
        bool isMidGesture() const noexcept;

        /** Fires when a mouse gesture ends, whether or not it changed anything. */
        std::function<void()> onGestureEnd;

        /** Keyboard undo/redo, forwarded to the editor. */
        std::function<void()> onUndoRequested, onRedoRequested;

        /** The type the Place tool is armed with. */
        SchematicModel::ElementType getPendingType() const noexcept { return pendingType; }

        //======================================================================
        /** Fires when the drawing changed in a way that affects the circuit. */
        std::function<void()> onSchematicChanged;

        /** Fires when the selection changed. */
        std::function<void()> onSelectionChanged;

        /** The part the inspector edits: the first selected, or null. */
        SchematicModel::Element* getSelectedElement() noexcept;

        /** Selected part ids, in the order selected. */
        const std::vector<int>& getSelection() const noexcept { return selectedIds; }

        /** Selected wires, kept apart from parts. */
        const std::vector<int>& getSelectedWires() const noexcept { return selectedWireIds; }

        void setSelectedWires(std::vector<int> ids);

        /** Where scope traces come from. Null draws empty scopes. */
        ScopeReader scopeReader;

        /** Replaces the selection (parts and wires) with a single notification. */
        void setSelection(std::vector<int> elementIds, std::vector<int> wireIds);

        /** Adds or removes a wire from the selection. */
        void toggleWireInSelection(int wireId);

        /** The part under a pixel position, or -1. Uses the artwork's bounds. */
        int elementAt(juce::Point<float> pixel) const noexcept;

        /** The wire under a pixel position, or -1. Parts take priority. */
        int wireAt(juce::Point<float> pixel) const noexcept;

        /** Grid units to pixels for the current zoom and pan. */
        juce::Point<float> pixelAt(juce::Point<float> gridPoint) const noexcept;

        /** Where a grid point is on screen. */
        juce::Point<float> gridToPixel(juce::Point<int> gridPoint) const noexcept
        {
            return painter.toPixel(gridPoint);
        }

        bool isSelected(int elementId) const noexcept;

        /** Whether a wire is selected. */
        bool isWireSelected(int wireId) const noexcept;

        /** Selects a part by id and scrolls it into view. */
        void selectElement(int elementId);

        /** Re-reads the schematic after an outside change. */
        void refresh();

        /** Frames the whole drawing in the visible area. */
        void zoomToFit();

        /** The space the floating panels cover on each side, so framing avoids them. */
        void setFramingInsets(juce::BorderSize<int> insets) { framingInsets = insets; }

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

        /** Ruler sizes in pixels: the top one stacks numbers above its ticks, the left one
            puts them beside. */
        static constexpr int topRulerThickness = 33;
        static constexpr int leftRulerThickness = 29;

       private:
        /** See setFramingInsets. */
        juce::BorderSize<int> framingInsets;

        /** The canvas minus the floating panels, or the whole canvas if too little is left. */
        juce::Rectangle<int> getFramingArea() const;

        //======================================================================
        /** One ruler. A separate opaque component, so moving the cursor marker repaints only
            the ruler, not the drawing. Ignores the mouse. */
        class Ruler : public juce::Component
        {
           public:
            Ruler(const SchematicCanvas& o, bool horizontal);
            void paint(juce::Graphics&) override;

            /** Cursor line position in this ruler's pixels, or negative when the mouse is away. */
            float cursor = -1.0f;

           private:
            const SchematicCanvas& owner;
            const bool isHorizontal;
        };

        Ruler topRuler{*this, true};
        Ruler leftRuler{*this, false};

        //======================================================================
        /** Opens a text editor over a part to type its value on the sheet. */
        void beginInlineEdit(SchematicModel::Element& element);
        void commitInlineEdit();
        void closeInlineEdit();

        /** Created when needed, destroyed when done. */
        std::unique_ptr<juce::TextEditor> inlineEditor;

        /** The part being edited, so a change underneath it can be detected. */
        int inlineEditId = -1;

        /** Draws each scope's trace over its symbol. */
        void drawScopeTraces(juce::Graphics&) const;

        /** Moves the ruler markers and repaints only the rulers. */
        void updateRulerCursor(juce::Point<float> position);
        void notifyChanged();

        /** Selects one part, or none with -1. */
        void setSelection(int elementId);

        /** Replaces the selection. */
        void setSelection(std::vector<int> ids);

        /** Adds or removes a part (shift-click). */
        void toggleInSelection(int elementId);

        /** Selects what the drawn box caught. */
        void applyBoxSelection(bool additive);

        /** The box being dragged, in pixels. */
        juce::Rectangle<float> getSelectionBox() const noexcept;

        /** True when dragging leftwards: select anything touched. */
        bool isCrossingBox() const noexcept { return boxCurrent.x < boxAnchor.x; }

        /** Wire line width at the current zoom. */
        float wireThickness() const noexcept
        {
            return juce::jmax(1.0f, painter.gridSize * 0.13f);
        }

        void paintGrid(juce::Graphics& g) const;

        /** Boxes, then wires and junctions, then parts. */
        void paintDrawing(juce::Graphics& g) const;

        /** Selection highlights and handles. */
        void paintSelection(juce::Graphics& g) const;

        /** The wire being drawn, the selection box, and the part about to be placed. */
        void paintInFlight(juce::Graphics& g) const;

        /** Cmd shortcuts: undo, redo, duplicate, rotate, flip. */
        bool commandKeyPressed(const juce::KeyPress& key);

        /** Places the armed part. */
        void placePendingElement(juce::Point<int> grid);

        /** Select tool mouse-down: handles, then parts, then wires, then the selection box. */
        void beginSelectGesture(const juce::MouseEvent& event, juce::Point<int> grid);

        /** Starts a corner or wire-end drag if the click is on one. */
        bool grabResizeHandle(const juce::MouseEvent& event, juce::Point<int> grid);

        /** Starts a move drag. */
        void beginDrag(juce::Point<int> grid);

        bool deleteAt(juce::Point<float> pixel);

        /** Whether the point is on the selected group box's resize corner. */
        bool isNearResizeHandle(const SchematicModel::Element& element,
                                juce::Point<int> grid) const noexcept;

        /** The wire showing end handles (a lone selected wire), or -1. Shared by painting and
            hit testing. */
        int wireWithHandles() const noexcept;

        /** Half-diagonal of a wire-end handle, in pixels. Shared by painting and hit testing. */
        float wireHandleReach() const noexcept;

        /** Which end of `wire` a pixel is on: 0 for a, 1 for b, -1 for neither. */
        int wireHandleAt(const SchematicModel::Wire& wire, juce::Point<float> pixel) const noexcept;

        /** Merges collinear wires the last gesture left touching, keeping `preferredId`, and
            drops selected ids that no longer exist. */
        void mergeWiresAfterEdit(int preferredId);

        /** Moves a group box's bottom-right corner to a grid point. */
        void resizeTo(SchematicModel::Element& element, juce::Point<int> grid);

        juce::Point<int> gridAt(juce::Point<float> pixel) const noexcept { return painter.toGrid(pixel); }

        SchematicModel::Schematic& schematic;
        SymbolPainter painter;

        /** Parts, wires and captions placed so far, rebuilt each paint for caption
            placement. */
        mutable CaptionObstacles captionObstacles;

        Tool tool = Tool::Select;
        /** The part being cloned by the Place tool, if any. */
        std::optional<SchematicModel::Element> pendingClone;

        SchematicModel::ElementType pendingType = SchematicModel::ElementType::Resistor;
        int pendingOrientation = 0;
        bool pendingMirrored = false;

        /** Selected part ids, oldest first. */
        std::vector<int> selectedIds;
        std::vector<int> selectedWireIds;

        // Drag state.
        bool draggingElement = false;
        bool resizingElement = false;

        /** Which end of the selected wire is being dragged, or -1. */
        int resizingWireEnd = -1;

        /** Last grid position of the drag, to move all selected parts by the delta. */
        juce::Point<int> dragLastGrid;

        /** Set once a drag moves something; a click without a drag narrows the selection to
            the clicked part. */
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
} // namespace Celine
