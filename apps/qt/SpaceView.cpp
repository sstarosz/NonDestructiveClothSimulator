#include "SpaceView.h"

#include <QGuiApplication>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPlatformSurfaceEvent>
#include <QVBoxLayout>
#include <QWindow>
#include <cmath>

#if QT_CONFIG(vulkan)
#include <QVulkanInstance>
#endif

#include "Theme.h"
#include "clothcad/render/SceneOf.h"

namespace clothcad::ui {

namespace {

// The view's background: between the two greys of the approved layout.
constexpr render::Colour background{0.2f, 0.21f, 0.235f};

// What a mouse event's position is, as a point.
QPointF where(const QMouseEvent* event) { return event->position(); }

// How far, in screen pixels, the mouse may move while pressed and still click.
constexpr double clickSlack = 4.0;

// What dragging a handle does, for the help line.
QString hintFor(render::GizmoHandle handle, session::GizmoMode mode) {
    using H = render::GizmoHandle;
    const auto axis = [](H h) { return h == H::AlongX || h == H::AboutX ? "x" : h == H::AlongY || h == H::AboutY ? "y" : "z"; };
    switch (handle) {
    case H::Middle:
        return mode == session::GizmoMode::Scale ? "Drag right to make the panel bigger, left to make it smaller. Ctrl: steps of 10 %."
                                                 : "Drag to move the panel across the view. Ctrl: whole centimetres.";
    case H::AlongX:
    case H::AlongY:
    case H::AlongZ:
        if (mode == session::GizmoMode::Scale && handle == H::AlongZ) return "A panel is flat: it has no thickness to scale along z.";
        if (mode == session::GizmoMode::Scale) return QString("Drag to stretch the panel along its own %1. Ctrl: steps of 10 %.").arg(axis(handle));
        return QString("Drag to move the panel along %1 only. Ctrl: whole centimetres.").arg(axis(handle));
    case H::PlaneYZ: return "Drag to move the panel in the y-z plane. Ctrl: whole centimetres.";
    case H::PlaneXZ: return "Drag to move the panel in the x-z plane. Ctrl: whole centimetres.";
    case H::PlaneXY:
        if (mode == session::GizmoMode::Scale) return "Drag to scale the panel along its own x and y together. Ctrl: steps of 10 %.";
        return "Drag to move the panel in the x-y plane. Ctrl: whole centimetres.";
    case H::AboutX:
    case H::AboutY:
    case H::AboutZ: return QString("Drag to turn the panel about %1. Ctrl: steps of 15°.").arg(axis(handle));
    case H::AboutView: return "Drag to turn the panel about the line of sight. Ctrl: steps of 15°.";
    case H::Free: return "Drag to turn the panel freely, as a ball. Ctrl: steps of 15°.";
    }
    return {};
}

} // namespace

// ---- Drawing straight into a window ----------------------------------------

// A native window Vulkan draws into, held in the widgets by a container.
// It only forwards what the hands do, and draws when asked.
class SpaceWindow : public QWindow {
public:
    explicit SpaceWindow(SpaceView& view) : view_(view) { setSurfaceType(QSurface::VulkanSurface); }
    ~SpaceWindow() override { swapchain_.reset(); }

    void draw() { requestUpdate(); }
    std::optional<QString> problem;  // why it could not draw, once it tried

protected:
    bool event(QEvent* event) override {
        switch (event->type()) {
        case QEvent::UpdateRequest: present(); return true;
        case QEvent::PlatformSurface:
            // The surface is about to go: so must the swapchain on it.
            if (static_cast<QPlatformSurfaceEvent*>(event)->surfaceEventType() == QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed) swapchain_.reset();
            break;
        default: break;
        }
        return QWindow::event(event);
    }
    void exposeEvent(QExposeEvent*) override {
        if (isExposed()) present();
    }
    void mousePressEvent(QMouseEvent* event) override { view_.press(where(event), event->button(), event->modifiers()); }
    void mouseMoveEvent(QMouseEvent* event) override { view_.moveTo(where(event), event->buttons(), event->modifiers()); }
    void mouseReleaseEvent(QMouseEvent*) override { view_.release(); }
    void wheelEvent(QWheelEvent* event) override { view_.wheel(event->angleDelta().y() / 120.0); }
    void keyPressEvent(QKeyEvent* event) override { view_.key(event->keyCombination()); }

private:
    void present() {
#if QT_CONFIG(vulkan)
        if (!isExposed() || !view_.renderer()) return;
        if (!swapchain_) {
            VkSurfaceKHR surface = QVulkanInstance::surfaceForWindow(this);
            if (!surface) {
                problem = "The window gave Vulkan no surface to draw in.";
                return;
            }
            auto made = view_.renderer()->swapchainFor(surface);
            if (!made) {
                problem = QString::fromStdString(made.error());
                return;
            }
            swapchain_ = std::move(*made);
        }
        const qreal scale = devicePixelRatio();
        const int width = static_cast<int>(std::lround(this->width() * scale)), height = static_cast<int>(std::lround(this->height() * scale));
        if (const auto shown = swapchain_->present(view_.sceneFor(width, height), width, height); !shown) problem = QString::fromStdString(shown.error());
#endif
    }

    SpaceView& view_;
    std::unique_ptr<render::Swapchain> swapchain_;
};

// ---- Drawing off screen and painting the picture ---------------------------

class SpacePicture : public QWidget {
public:
    explicit SpacePicture(SpaceView& view, QWidget* parent) : QWidget(parent), view_(view) {
        setMouseTracking(true);  // the gizmo's handles light up under the mouse
        setFocusPolicy(Qt::ClickFocus);
    }
    void draw() {
        const qreal scale = devicePixelRatioF();
        const int width = static_cast<int>(std::lround(this->width() * scale)), height = static_cast<int>(std::lround(this->height() * scale));
        if (!view_.renderer() || width <= 0 || height <= 0) return;
        auto drawn = view_.renderer()->draw(view_.sceneFor(width, height), width, height);
        if (!drawn) return;
        picture = std::move(*drawn);
        image_ = QImage(picture->pixels.data(), picture->width, picture->height, picture->width * 4, QImage::Format_RGBA8888);
        image_.setDevicePixelRatio(scale);
        update();
    }
    std::optional<render::Picture> picture;

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        if (image_.isNull()) painter.fillRect(rect(), QColor::fromRgbF(background.red, background.green, background.blue));
        else painter.drawImage(QPointF(0, 0), image_);
    }
    void resizeEvent(QResizeEvent*) override { draw(); }
    void mousePressEvent(QMouseEvent* event) override { view_.press(where(event), event->button(), event->modifiers()); }
    void mouseMoveEvent(QMouseEvent* event) override { view_.moveTo(where(event), event->buttons(), event->modifiers()); }
    void mouseReleaseEvent(QMouseEvent*) override { view_.release(); }
    void wheelEvent(QWheelEvent* event) override { view_.wheel(event->angleDelta().y() / 120.0); }
    void keyPressEvent(QKeyEvent* event) override { view_.key(event->keyCombination()); }

private:
    SpaceView& view_;
    QImage image_;  // shares the picture's pixels
};

// ---- The view ------------------------------------------------------------------

SpaceView::SpaceView(QWidget* parent) : QWidget(parent) {
    setObjectName("space");
    setMinimumSize(200, 200);
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    time_ = new QLabel(this);
    time_->setObjectName("spaceTime");
    time_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    time_->setContentsMargins(8, 2, 8, 2);
    time_->setStyleSheet(QString("color: %1").arg(theme::quiet.name()));
    column->addWidget(time_);

    // A window of its own where the system has one for Vulkan; the off-screen
    // platform has none, so there the picture is read back and painted.
    const bool windows = QGuiApplication::platformName() != "offscreen";
    auto made = render::Renderer::create({.forWindows = windows});
    if (!made) {
        problem_ = QString::fromStdString(made.error());
    } else {
        renderer_ = std::move(*made);
#if QT_CONFIG(vulkan)
        if (windows) {
            vulkan_ = std::make_unique<QVulkanInstance>();
            vulkan_->setVkInstance(reinterpret_cast<VkInstance>(renderer_->instance()));
            if (vulkan_->create()) {
                window_ = new SpaceWindow(*this);
                window_->setVulkanInstance(vulkan_.get());
                windowHolder_ = QWidget::createWindowContainer(window_, this);
                windowHolder_->setFocusPolicy(Qt::ClickFocus);
                column->addWidget(windowHolder_, 1);
                drawing_ = Drawing::Window;
            } else {
                vulkan_.reset();
            }
        }
#endif
        if (drawing_ == Drawing::Nothing) {
            picture_ = new SpacePicture(*this, this);
            column->addWidget(picture_, 1);
            drawing_ = Drawing::Picture;
        }
    }
    if (drawing_ == Drawing::Nothing) {
        why_ = new QLabel("The 3D view needs Vulkan. " + problem_, this);
        why_->setWordWrap(true);
        why_->setAlignment(Qt::AlignCenter);
        why_->setStyleSheet(QString("color: %1; padding: 24px").arg(theme::quiet.name()));
        column->addWidget(why_, 1);
    }
}

SpaceView::~SpaceView() {
    // The window and its swapchain go first, then Qt's view of the instance, then the renderer.
    delete windowHolder_;
    windowHolder_ = nullptr;
    window_ = nullptr;
    vulkan_.reset();
}

const render::Picture* SpaceView::picture() const { return picture_ && picture_->picture ? &*picture_->picture : nullptr; }

void SpaceView::show(const session::Session& session, std::optional<PanelId> selected, session::Tool tool, bool mesh, bool body,
                     std::optional<SeamId> seam) {
    if (const std::optional<session::GizmoMode> mode = session::gizmoOf(tool); mode != mode_) {
        mode_ = mode;
        hovered_.reset();
        redraw();
    }
    // The scene is kept even where nothing can be drawn: a click is picked in it.
    draped_ = session.clothShown();
    const Shown now{session.sceneVersion(), selected, mesh, body, seam, pixelScale()};
    if (shown_ == now) return;
    shown_ = now;
    selected_ = selected;
    gizmo_ = selected ? render::gizmoOf(session, *selected, session::GizmoMode::Move, 1.0) : std::nullopt;
    scene_ = render::sceneOf(session, camera_, 1.0, {.selected = selected, .body = body, .mesh = mesh, .seam = seam, .pixel = now.pixel});
    scene_.background = background;
    extent_ = render::extentOf(session);
    if (!framed_ && extent_) {
        camera_.frame(extent_->first, extent_->second);
        framed_ = true;
    }
    redraw();
}

void SpaceView::frame() {
    if (extent_) camera_.frame(extent_->first, extent_->second);
    redraw();
}

void SpaceView::showTime(std::optional<double> seconds, bool sewing) {
    const QString now = seconds ? QString("%1 s%2").arg(*seconds, 0, 'f', 1).arg(sewing ? "  ·  sewing" : "") : QString();
    if (now != time_->text()) time_->setText(now);
}

render::Scene SpaceView::sceneFor(int width, int height) const {
    render::Scene scene = scene_;
    scene.viewProjection = camera_.viewProjection(width / std::max(1.0, static_cast<double>(height)));
    const sim::Vec3 light = sim::normalized(camera_.toEye() + sim::Vec3{0.0, 0.4, 0.0});
    scene.toLight = {static_cast<float>(light.x), static_cast<float>(light.y), static_cast<float>(light.z)};
    if (const std::optional<render::Gizmo> shown = gizmo())
        render::addGizmo(scene, *shown, camera_, {width, height}, carrying_ && carrying_->byGizmo ? std::optional(carrying_->handle) : hovered_);
    render::addCornerAxes(scene, camera_, {width, height}, pixelScale());
    return scene;
}

double SpaceView::pixelScale() const {
    if (window_) return window_->devicePixelRatio();
    if (picture_) return picture_->devicePixelRatioF();
    return devicePixelRatioF();
}

QSize SpaceView::surfaceSize() const {
    if (window_) return window_->size();
    if (picture_) return picture_->size();
    return size();
}

render::PicturePoint SpaceView::inPicture(QPointF at) const { return {at.x() * pixelScale(), at.y() * pixelScale()}; }

render::ViewSize SpaceView::pictureSize() const {
    const QSize surface = surfaceSize();
    return {std::max(1, static_cast<int>(std::lround(surface.width() * pixelScale()))), std::max(1, static_cast<int>(std::lround(surface.height() * pixelScale())))};
}

std::optional<render::Gizmo> SpaceView::gizmo() const {
    if (!gizmo_ || !mode_) return std::nullopt;
    render::Gizmo shown = *gizmo_;
    shown.mode = *mode_;
    shown.pixel = pixelScale();
    return shown;
}

void SpaceView::redraw() {
    if (window_) window_->draw();
    if (picture_) picture_->draw();
}

// Maya's hands: Alt with the left button orbits, with the middle pans, with
// the right dollies. The left button alone picks a panel, or takes hold of
// the selected panel's gizmo.
void SpaceView::press(QPointF at, Qt::MouseButton button, Qt::KeyboardModifiers keys) {
    hand_ = Hand::None;
    carrying_.reset();
    pulling_.reset();
    if (keys & Qt::AltModifier) hand_ = button == Qt::LeftButton ? Hand::Orbit : button == Qt::MiddleButton ? Hand::Pan : button == Qt::RightButton ? Hand::Dolly : Hand::None;
    else if (button == Qt::MiddleButton) hand_ = Hand::Pan;  // the middle button pans without Alt too
    else if (button == Qt::LeftButton) {
        // The gizmo first, then the cloth under the mouse: pulled when draped,
        // carried when it is the selected panel; anything else is a click.
        hand_ = Hand::Pick;
        const std::optional<render::Gizmo> shown = gizmo();
        const std::optional<render::GizmoHandle> handle = shown ? render::handleAt(*shown, camera_, pictureSize(), inPicture(at)) : std::nullopt;
        if (handle && selected_) {
            carrying_ = Carrying{.panel = *selected_, .gizmo = *shown, .handle = *handle, .byGizmo = true, .by = {}, .turn = {}, .scale = {}};
        } else if (const std::optional<render::ClothHit> hit = render::clothAt(scene_, camera_, pictureSize(), inPicture(at))) {
            if (draped_) pulling_ = Pulling{*hit, hit->at};
            else if (hit->panel == selected_ && mode_ == session::GizmoMode::Move)
                carrying_ = Carrying{.panel = hit->panel,
                                     .gizmo = {.mode = session::GizmoMode::Move, .centre = hit->at, .pixel = pixelScale()},
                                     .handle = render::GizmoHandle::Middle,
                                     .byGizmo = false,
                                     .by = {},
                                     .turn = {},
                                     .scale = {}};
        }
    }
    handAt_ = at;
    pressedAt_ = at;
    movedFromPress_ = false;
}

void SpaceView::moveTo(QPointF at, Qt::MouseButtons held, Qt::KeyboardModifiers keys) {
    if (hand_ == Hand::None || held == Qt::NoButton) {
        // Only hovering: light the gizmo's handle under the mouse.
        std::optional<render::GizmoHandle> under;
        if (const std::optional<render::Gizmo> shown = gizmo()) under = render::handleAt(*shown, camera_, pictureSize(), inPicture(at));
        if (under != hovered_) {
            hovered_ = under;
            if (under && mode_) emit hinted(hintFor(*under, *mode_));
            redraw();
        }
        return;
    }
    const QPointF moved = at - handAt_;
    handAt_ = at;
    if (!movedFromPress_ && std::hypot(at.x() - pressedAt_.x(), at.y() - pressedAt_.y()) > clickSlack) movedFromPress_ = true;
    switch (hand_) {
    case Hand::Orbit: camera_.orbit(moved.x(), moved.y()); break;
    case Hand::Pan: camera_.pan(moved.x(), moved.y(), std::max(1, height() - time_->height())); break;
    case Hand::Dolly: camera_.dolly(std::pow(1.005, -(moved.x() + moved.y()))); break;  // right or down comes nearer
    case Hand::Pick:
        if (pulling_ && movedFromPress_) {
            if (const std::optional<sim::Vec3> by = render::acrossTheView(camera_, pictureSize(), pulling_->held.at, inPicture(pressedAt_), inPicture(at))) {
                pulling_->to = pulling_->held.at + *by;
                const session::DragPhase phase = pullingStarted_ ? session::DragPhase::Continue : session::DragPhase::Begin;
                pullingStarted_ = true;
                emit clothPulled(pulling_->held.panel, pulling_->held.point, pulling_->to, phase);
            }
        }
        if (carrying_ && movedFromPress_) {
            const bool snapping = keys & Qt::ControlModifier;
            const render::PicturePoint from = inPicture(pressedAt_), to = inPicture(at);
            const session::DragPhase phase = carryingStarted_ ? session::DragPhase::Continue : session::DragPhase::Begin;
            switch (carrying_->gizmo.mode) {
            case session::GizmoMode::Move:
                // Whole centimetres with Ctrl, else millimetres.
                if (const std::optional<sim::Vec3> by = render::moveBy(carrying_->gizmo, carrying_->handle, camera_, pictureSize(), from, to, snapping ? 1.0 : 0.1)) {
                    carrying_->by = *by;
                    carryingStarted_ = true;
                    emit gestured(session::MoveInSpace{carrying_->panel, *by, phase});
                }
                break;
            case session::GizmoMode::Rotate:
                if (const std::optional<render::Turn> turn = render::turnBy(carrying_->gizmo, carrying_->handle, camera_, pictureSize(), from, to, snapping ? 15.0 : 0.1)) {
                    carrying_->turn = *turn;
                    carryingStarted_ = true;
                    emit gestured(session::TurnInSpace{carrying_->panel, turn->axis, turn->degrees, phase});
                }
                break;
            case session::GizmoMode::Scale:
                if (const std::optional<session::Factors> scale = render::scaleBy(carrying_->gizmo, carrying_->handle, camera_, pictureSize(), from, to, snapping ? 0.1 : 0.01)) {
                    carrying_->scale = *scale;
                    carryingStarted_ = true;
                    emit gestured(session::ScalePanel{carrying_->panel, scale->x, scale->y, phase});
                }
                break;
            }
        }
        return;  // the picture follows the document
    case Hand::None: break;
    }
    redraw();
}

void SpaceView::release() {
    if (hand_ == Hand::Pick) {
        if (carrying_ && carryingStarted_) {
            const session::DragPhase end = session::DragPhase::End;
            switch (carrying_->gizmo.mode) {
            case session::GizmoMode::Move: emit gestured(session::MoveInSpace{carrying_->panel, carrying_->by, end}); break;
            case session::GizmoMode::Rotate: emit gestured(session::TurnInSpace{carrying_->panel, carrying_->turn.axis, carrying_->turn.degrees, end}); break;
            case session::GizmoMode::Scale: emit gestured(session::ScalePanel{carrying_->panel, carrying_->scale.x, carrying_->scale.y, end}); break;
            }
        }
        else if (pulling_ && pullingStarted_) emit clothPulled(pulling_->held.panel, pulling_->held.point, pulling_->to, session::DragPhase::End);
        else if (!(carrying_ && carrying_->byGizmo) && !movedFromPress_) emit panelClicked(render::panelAt(scene_, camera_, pictureSize(), inPicture(pressedAt_)));
    }
    hand_ = Hand::None;
    carrying_.reset();
    carryingStarted_ = false;
    pulling_.reset();
    pullingStarted_ = false;
    redraw();
}

void SpaceView::wheel(double steps) {
    camera_.dolly(std::pow(0.88, steps));
    redraw();
}

void SpaceView::key(QKeyCombination pressed) {
    if (pressed == QKeyCombination(Qt::Key_F)) frame();
    else emit keyed(pressed);
}

} // namespace clothcad::ui
