#pragma once
// The 2D table: every panel's outline where the table has it, seams in
// blue, the selected panel in amber, mirror axes in pink. With the Select
// tool (Q) a panel is chosen and dragged, its corners and curve handles are
// dragged (Shift adds corners, Shift-drag selects corners in a box; Shift
// held while a corner is dragged keeps it on the x or y it first moved
// along, Ctrl snaps the other to the grid), and a
// double-click on an edge types its length. Move (W), Rotate (E) and Scale
// (R) do the same and show their gizmo on the selected panel
// (session/TableGizmo): its arrows move it, its ring turns it about its
// middle, its handles scale it.
// The drawing tools work here too: Rectangle and Circle are dragged out,
// Ring is a click, Point, Curve, Mirror and Sew click edges, Line clicks
// two points on one outline, Cut and Split click an internal line.
// Holding Ctrl snaps to the grid; F frames every panel; Esc drops what is
// half done. Delete removes the selected corners that were added with the
// Point tool, or, with no corner selected, the selected panel; Ctrl+C
// remembers the selected panel, Ctrl+V pastes a copy with its middle under
// the pointer, and Ctrl+D duplicates it beside the original. The keys work
// while the table has the focus (a click on it gives it). It only reports
// what the user did: the window hands each gesture to session::Drafting.
//
//   table->show(doc, shapes, selected, corners, drafting.tool);
//   connect(table, &TableView::gestured, [&](const session::Gesture& g) { ... });

#include <QImage>
#include <QWidget>
class QPainter;
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Shelf.h"
#include "clothcad/core/Document.h"
#include "clothcad/core/Drawing.h"
#include "clothcad/core/Evaluate.h"
#include "clothcad/core/Fields.h"
#include "clothcad/session/Drafting.h"
#include "clothcad/session/Lengths.h"
#include "clothcad/session/TableGizmo.h"
#include "clothcad/sim/Mesh.h"

namespace clothcad::ui {

class TableView : public QWidget {
    Q_OBJECT
public:
    explicit TableView(QWidget* parent = nullptr);
    // What to show, and with which tool: another tool than before drops what was half done.
    void show(const Document& doc, const Result& shapes, std::optional<PanelId> selected, const std::vector<CornerRef>& corners, Tool tool);
    // The cloth mesh to draw over the panels, or none. The table keeps a share of it.
    void setMesh(std::shared_ptr<const sim::ClothMesh> mesh);
    bool meshShown() const { return mesh_ != nullptr; }
    // Asks the user to click an edge, corner or line of one panel, for a
    // layer's reference: the candidates are what that layer could use, in
    // the panel's own coordinates. Empty: nothing is being chosen.
    struct Choice {
        enum class Kind { Edge, Corner, Line };
        PanelId panel;
        Kind kind;
        Pattern candidates;
    };
    void choose(std::optional<Choice> choice);
    bool choosing() const { return choosing_.has_value(); }
    void fit();     // every panel in view
    // Whether the grid is drawn; snapping to it with Ctrl works either way.
    void setGridShown(bool shown);
    bool gridShown() const { return gridShown_; }
    // For tests: the grid's step as written in the corner, where the length
    // labels were last painted, and the links between the ends of each seam.
    QString gridKey() const;
    const std::vector<std::pair<QRectF, EdgeRef>>& lengthLabels() const { return lengthLabels_; }
    const std::vector<std::pair<Vec2, Vec2>>& seamLinks() const { return seamLinks_; }
    void cancel();  // forgets a drag in progress

    // Table point <-> widget pixel, for tests and gestures.
    QPointF toScreen(Vec2 tablePoint) const;
    Vec2 toTable(QPointF screen) const;
    double scale() const { return scale_; }
    // The selected panel's gizmo as it stands now, in table cm; empty with
    // Select, a drawing tool, or nothing selected. For tests.
    std::optional<session::TableGizmo> gizmo() const;

signals:
    void gestured(const clothcad::session::Gesture& gesture);
    void pointerAt(clothcad::Vec2 tablePoint);
    void told(const QString& why);     // why what was pressed cannot be dragged
    void hinted(const QString& next);  // what to do next, in a tool of two clicks
    void referencePicked(const clothcad::Reference& reference);
    void choosingDropped();  // Esc while choosing

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    // A panel as drawn: its outline turned and moved to its place on the table.
    struct Drawn {
        PanelId panel;
        std::string name;
        Pattern pattern;  // in table coordinates
        std::vector<std::vector<Vec2>> sewn;
        std::vector<std::pair<Vec2, Vec2>> axes;
        bool instance = false;
        Vec2 place;       // the panel's place on the table
        double angle = 0; // its angle on the table, degrees
        Vec2 pivot;       // where it turns about: the centre of its own box, on the table
        // Selected panel only: why a corner (by vertex) or a handle (by edge,
        // start then end) cannot be dragged; empty when it can.
        std::vector<std::string> cornerLocks;
        std::vector<std::pair<std::string, std::string>> handleLocks;
        // Selected panel only: its edges' lengths, each with the formula that sets it, if one does.
        std::vector<session::EdgeLength> lengths;
    };

    // What the pointer would take hold of on the selected panel.
    struct Grab {
        enum class Kind { Panel, Corner, Handle, Gizmo, Box } kind;
        PanelId panel;
        CornerRef corner{};
        EdgeRef edge{};
        bool endHandle = false;
        Vec2 thingWasAt{};  // where the panel's place, the corner or the handle was
        session::TableHandle handle = session::TableHandle::Middle;  // the gizmo's handle taken hold of
    };
    struct Drag {
        Grab grab;
        Vec2 pressedAt;
        QPointF pressedOnScreen;
        double angleWas = 0;
        bool moved = false;
        std::optional<session::TableGizmo> gizmo;  // the gizmo as it stood when it was taken hold of
        session::Along along = session::Along::Both;  // a corner held to one axis (Shift), once the pointer has chosen it
    };

    // What a drawing tool would act on, under the pointer.
    struct Offer {
        PanelId panel;
        std::optional<EdgeRef> edge{};        // Curve, Mirror, Sew
        std::optional<OutlinePoint> point{};  // Point, Line
        std::optional<LineRef> line{};        // Cut, Split
        Cut::Side keep = Cut::Side::Left;     // Cut: the side that stays
        QString note{};                       // shown by the pointer: "26.0 cm", "40 % along"
    };

    const Drawn* chosen() const;
    const Drawn* drawnOf(PanelId panel) const;
    std::optional<Offer> offerAt(QPointF screen) const;
    void pressWithTool(QPointF screen, bool snapping);
    std::optional<Reference> choiceAt(QPointF screen) const;
    void paintChoice(QPainter& painter) const;
    void paintTool(QPainter& painter) const;
    // Select, Move, Rotate or Scale: the tools that select and drag on the table.
    bool selecting() const;
    void paintGizmo(QPainter& painter) const;
    // The gizmo's handle, a curve handle or a corner of the selected panel under a screen point;
    // `locked` says why when it is one that cannot be dragged.
    std::optional<Grab> grabAt(QPointF screen, std::string& locked) const;
    std::optional<EdgeRef> edgeAt(QPointF screen) const;
    Vec2 snapped(Vec2 point, bool snapping) const;
    double gridStep() const;
    bool gridShown_ = true;
    // The table's floor: the background and the grid. Drawn once and painted
    // again as long as the view does not move. Its hundreds of lines cost more
    // than everything else on the table together (16 ms on a 4K table), and
    // every mouse move paints the table again.
    struct Floor {
        QImage image;
        QSize size;
        qreal ratio = 0.0;
        Vec2 centre{0, 0};
        double scale = 0.0;
        bool grid = false;
    };
    void paintFloor(QPainter& painter);
    Floor floor_;
    // `oneAxis`: Shift is down, so a corner goes along x or y only.
    void dragTo(QPointF screen, bool snapping, bool oneAxis, bool released);

    std::vector<Drawn> drawn_;
    // The ends of each seam's edges that meet, on the table: crossed links show a twist.
    std::vector<std::pair<Vec2, Vec2>> seamLinks_;
    // Where the length labels were painted, for a double-click on one.
    mutable std::vector<std::pair<QRectF, EdgeRef>> lengthLabels_;
    std::optional<PanelId> selected_;
    std::vector<CornerRef> corners_;
    std::shared_ptr<const sim::ClothMesh> mesh_;
    Tool tool_ = Tool::Select;
    Vec2 centre_{0, 0};   // the table point in the middle of the widget
    double scale_ = 4.0;  // pixels per cm
    bool fitted_ = false;
    QPointF panFrom_;

    std::optional<Drag> drag_;
    std::optional<QPointF> offered_;  // the grab the pointer is over, to ring it
    std::optional<session::TableHandle> gizmoOffered_;  // the gizmo's handle the pointer is over, to light it
    QString readout_;                 // what a drag is doing: "+1.5, -2.0 cm", "30.0°"
    QPointF pointer_;
    bool pointerInside_ = false;  // the pointer has moved over the table and not left it since

    // A layer's reference being chosen: the candidates placed on the table.
    struct Choosing {
        PanelId panel;
        Choice::Kind kind;
        Pattern candidates;
    };
    std::optional<Choosing> choosing_;
    std::optional<Reference> choiceOffered_;

    // A drawing tool's work in progress.
    std::optional<Offer> offer_;          // what the pointer is over
    std::optional<Vec2> drawFrom_;        // Rectangle, Circle: where the drag began, on the table
    std::optional<SeamSide> sewFrom_;     // Sew: the first edge
    struct LineStart {
        PanelId panel;
        OutlinePoint point;
    };
    std::optional<LineStart> lineFrom_;  // Line: the first end
};

} // namespace clothcad::ui
