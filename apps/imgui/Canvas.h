#pragma once
// The viewport: every panel on the table, and the tools that work on them.

#include <vector>
#include <memory>
#include <optional>

#include "Tools.h"
#include "clothcad/sim/Mesh.h"
#include "clothcad/core/Document.h"
#include "clothcad/core/Evaluate.h"
#include "clothcad/core/Fields.h"

namespace clothcad::app {

// A request for the user to click something on one panel. It comes from the
// details pane, when an operation needs an edge, corner or line chosen.
struct PickRequest {
    enum class Kind { Edge, Corner, Line };

    PanelId panel;
    Kind kind;
    Pattern candidates;  // the things that may be picked, in that panel's own coordinates
};

// What the user did on the canvas this frame.
struct CanvasEvents {
    std::optional<Gesture> gesture;
    std::optional<Reference> picked;  // only while a PickRequest is shown
};

class Canvas {
public:
    // Zooms and centres on all panels the next time the canvas is drawn.
    void fit() { fitRequested_ = true; }

    // The measuring grid behind the panels.
    bool gridVisible() const { return gridVisible_; }
    void setGridVisible(bool visible) { gridVisible_ = visible; }

    // The corners the window has selected on the selected panel; drawn highlighted.
    void setSelectedCorners(std::vector<CornerRef> corners) { selectedCorners_ = std::move(corners); }

    // The cloth mesh to draw over the panels, or none. The canvas keeps a share of it.
    void setMesh(std::shared_ptr<const sim::ClothMesh> mesh) { mesh_ = std::move(mesh); }

    // Forgets anything half done: a line with one end placed, a drag.
    void cancel();

    // Draws into the current ImGui window, filling what is left of it.
    // With a `pick`, clicks choose one of its candidates and the tool rests.
    // With `lookOnly`, panels can be selected and moved but nothing reshaped:
    // used while the details pane shows a panel at an earlier step.
    CanvasEvents draw(const Document& doc, const Result& shapes, std::optional<PanelId> selected, Tool tool,
                      const PickRequest* pick = nullptr, bool lookOnly = false);

    // Where a point on the table was drawn, in screen pixels, at the last draw.
    struct ScreenPoint {
        float x, y;
    };
    ScreenPoint toScreen(Vec2 tablePoint) const;

private:
    float zoom_ = 4.0f;  // pixels per centimetre
    float panX_ = 0.0f;  // where the table's origin sits, in pixels from the canvas' top-left
    float panY_ = 0.0f;
    float originX_ = 0.0f;  // the canvas' top-left on screen, at the last draw
    float originY_ = 0.0f;
    bool fitRequested_ = true;
    bool gridVisible_ = true;

    // ---- What is in progress ----
    struct Drag {
        enum class Kind { Panel, Corner, Handle, Rotate, Box } kind;
        PanelId panel;
        CornerRef corner;        // Kind::Corner
        EdgeRef edge;            // Kind::Handle
        bool endHandle = false;  // Kind::Handle
        Vec2 pressedAt;          // where the cursor was, on the table
        Vec2 thingWasAt;         // where the panel's origin, the corner or the handle was, on the table
        bool moved = false;
        double angle0 = 0.0;     // Kind::Rotate: the panel's angle when the drag began
    };
    std::optional<Drag> drag_;
    std::vector<CornerRef> selectedCorners_;
    std::shared_ptr<const sim::ClothMesh> mesh_;
    std::optional<Vec2> rectangleFrom_;  // Rectangle and Circle tools: where the drag started, on the table
    struct LineStart {
        PanelId panel;
        OutlinePoint point;
    };
    std::optional<LineStart> lineFrom_;
    std::optional<SeamSide> sewFrom_;    // Sew tool: the first edge, once clicked  // Line tool: the first end, once placed
    Tool lastTool_ = Tool::Select;
};

} // namespace clothcad::app
