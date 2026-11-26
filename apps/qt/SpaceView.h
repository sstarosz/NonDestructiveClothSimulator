#pragma once
// The 3D view: the body, the cloth and the floor, drawn with our own Vulkan
// renderer (libs/render). The camera works as Maya's: Alt with the left
// button orbits, with the middle button pans, with the right button dollies;
// the wheel dollies too and F frames everything.
//
// A click with the left button picks the panel under it (or none, on the
// body or the floor). With Move (W), Rotate (E) or Scale (R) the selected
// panel shows that gizmo (render/Gizmo): arrows and squares move it, rings
// turn it, handles scale it; Ctrl moves in whole centimetres, turns in steps
// of 15° and scales in steps of 10 %. With Move, dragging the selected panel
// itself carries it too. Select (Q) shows no gizmo and only selects. Once the
// cloth is draped, dragging the cloth pulls it by the particle under the
// mouse, as in Marvelous Designer, whatever the tool. The view only reports
// these, as session gestures; the main window hands them to session::Drafting.
//
// Where the system lets Vulkan draw into a window, the picture goes straight
// to the screen. Where it does not (the off-screen platform the tests use),
// the same picture is drawn off screen and painted. Without Vulkan at all,
// the view says why.
//
//   space->show(session, selected, drafting.tool, meshShown);  // each frame; draws only what changed
//   connect(space, &SpaceView::gestured, [&](const session::Gesture& g) { ... });
//   space->showTime(seconds, sewing);

#include <QWidget>
#include <cstdint>
#include <memory>
#include <optional>

#include "clothcad/render/Camera.h"
#include "clothcad/render/Gizmo.h"
#include "clothcad/render/Picking.h"
#include "clothcad/render/Renderer.h"
#include "clothcad/render/Scene.h"
#include "clothcad/session/Drafting.h"
#include "clothcad/session/Session.h"

class QLabel;
class QVulkanInstance;

namespace clothcad::ui {

class SpaceWindow;
class SpacePicture;

class SpaceView : public QWidget {
    Q_OBJECT
public:
    explicit SpaceView(QWidget* parent = nullptr);
    ~SpaceView() override;

    // What to show, and with which tool: it says which gizmo the selected panel shows.
    void show(const session::Session& session, std::optional<PanelId> selected, session::Tool tool, bool mesh, bool body = true,
              std::optional<SeamId> seam = {});
    void showTime(std::optional<double> seconds, bool sewing);
    void frame();  // everything in view

    // How the view draws, for the help line and for tests.
    enum class Drawing { Window, Picture, Nothing };
    Drawing drawing() const { return drawing_; }
    QString problem() const { return problem_; }
    const render::OrbitCamera& camera() const { return camera_; }
    // The last picture drawn off screen (Drawing::Picture), for tests.
    const render::Picture* picture() const;

    // What the hands do to the camera, from whichever surface they are on.
    void press(QPointF at, Qt::MouseButton button, Qt::KeyboardModifiers keys);
    void moveTo(QPointF at, Qt::MouseButtons held, Qt::KeyboardModifiers keys);
    void release();
    void wheel(double steps);
    void key(QKeyCombination pressed);

    // The scene as it should be drawn into a picture of this size, in pixels.
    render::Scene sceneFor(int width, int height) const;
    render::Renderer* renderer() const { return renderer_.get(); }

    // Where a place given in the view's own coordinates (as press() takes
    // them) is in the picture, and the picture's size: for picking, and for tests.
    render::PicturePoint inPicture(QPointF at) const;
    render::ViewSize pictureSize() const;
    // The selected panel's gizmo, as it is drawn now; empty when no panel is
    // selected or the tool has no gizmo.
    std::optional<render::Gizmo> gizmo() const;

signals:
    // A click (not a drag) with the left button: the panel under it, if any.
    void panelClicked(std::optional<clothcad::PanelId> panel);
    // The selected panel dragged by its gizmo or by itself: carried
    // (MoveInSpace), turned (TurnInSpace) or scaled (ScalePanel).
    void gestured(const clothcad::session::Gesture& gesture);
    // The draped cloth pulled by a particle (an index into its panel's mesh) towards `to`, in space.
    void clothPulled(clothcad::PanelId panel, std::uint32_t point, clothcad::sim::Vec3 to, clothcad::session::DragPhase phase);
    // What the handle under the mouse does.
    void hinted(const QString& next);
    // A key the view does not use itself (Q, W, E, R, Space, Delete, Ctrl+C ...): the window may.
    void keyed(QKeyCombination pressed);
    // A new picture was drawn, which took that long (View > Frame times).
    void drawn(double milliseconds);

private:
    void redraw();
    // Pixels of the picture per pixel of the screen's own scale.
    double pixelScale() const;
    // The surface the picture is drawn on and the mouse is reported from.
    QSize surfaceSize() const;

    std::unique_ptr<render::Renderer> renderer_;
    std::unique_ptr<QVulkanInstance> vulkan_;  // Qt's view of the renderer's instance; goes before it
    SpaceWindow* window_ = nullptr;
    QWidget* windowHolder_ = nullptr;
    SpacePicture* picture_ = nullptr;
    QLabel* time_ = nullptr;
    QLabel* why_ = nullptr;
    Drawing drawing_ = Drawing::Nothing;
    QString problem_;

    render::OrbitCamera camera_;
    render::Scene scene_;  // without its camera, which is put in when it is drawn
    std::optional<std::pair<sim::Vec3, sim::Vec3>> extent_;
    std::optional<PanelId> selected_;          // the panel the gizmo moves
    bool draped_ = false;                      // the cloth is simulated: dragging it pulls it
    std::optional<render::Gizmo> gizmo_;       // its gizmo where it stands, turned as it is
    std::optional<session::GizmoMode> mode_;   // which gizmo it shows; none: Select
    bool framed_ = false;
    // What the scene was made from, to make it again only when that changed.
    struct Shown {
        std::uint64_t version = 0;
        std::optional<PanelId> selected;
        bool mesh = false;
        bool body = true;
        std::optional<SeamId> seam{};
        double pixel = 1.0;
        bool operator==(const Shown&) const = default;
    };
    std::optional<Shown> shown_;

    enum class Hand { None, Orbit, Pan, Dolly, Pick };
    Hand hand_ = Hand::None;
    QPointF handAt_;
    QPointF pressedAt_;
    bool movedFromPress_ = false;  // the button held has moved beyond a click

    // The selected panel held, by a handle of its gizmo or by a spot on it
    // (then `gizmo` is a Move gizmo at that spot and `handle` is its Middle).
    struct Carrying {
        PanelId panel;
        render::Gizmo gizmo;
        render::GizmoHandle handle;
        bool byGizmo = true;
        sim::Vec3 by;               // the last move reported
        render::Turn turn;          // the last turn reported
        session::Factors scale;     // the last scale reported
    };
    std::optional<Carrying> carrying_;
    // The draped cloth held by a particle.
    struct Pulling {
        render::ClothHit held;
        sim::Vec3 to;  // where the hand was last reported
    };
    std::optional<Pulling> pulling_;
    bool pullingStarted_ = false;
    bool carryingStarted_ = false;  // the drag of the gizmo has been reported to begin
    std::optional<render::GizmoHandle> hovered_;
};

} // namespace clothcad::ui
