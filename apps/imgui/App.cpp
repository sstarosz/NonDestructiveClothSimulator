#include "App.h"

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <exception>
#include <variant>

#include <imgui.h>

#include "Locks.h"
#include "Style.h"
#include "clothcad/core/Drawing.h"
#include "clothcad/core/Evaluate.h"
#include "clothcad/core/Fields.h"
#include "clothcad/core/File.h"
#include "clothcad/core/Text.h"

namespace clothcad::app {

namespace {

void errorText(const std::string& message) {
    ImGui::PushStyleColor(ImGuiCol_Text, color::error);
    ImGui::TextWrapped("%s", message.c_str());
    ImGui::PopStyleColor();
}

// "base1", "base2" ...: the first that `taken` says is free.
template <class Taken>
std::string firstFree(const std::string& base, Taken taken) {
    for (int number = 1;; ++number) {
        const std::string name = base + std::to_string(number);
        if (!taken(name)) return name;
    }
}

bool parameterNamed(const Document& doc, const std::string& name) {
    for (Param p : doc.parameters())
        if (doc.get(p).name == name) return true;
    return false;
}

bool panelNamed(const Document& doc, const std::string& name) {
    for (PanelId p : doc.panels())
        if (doc.get(p).name == name) return true;
    return false;
}

// ---- The operations the "Add operation" menu offers ------------------------------------

struct Offered {
    const char* name;
    const char* hint;
    bool starting;  // creates the outline, so it is offered for an empty panel only
    OpData (*make)(const Document& doc, PanelId panel);
};

OpData makeRect(const Document&, PanelId) { return Rect{.width = 20_cm, .height = 20_cm}; }
OpData makeCircle(const Document&, PanelId) { return Circle{.radius = 20_cm}; }
OpData makeRing(const Document&, PanelId) { return RingSector{.innerLength = 70_cm, .sweep = 180_deg, .depth = 60_cm}; }
OpData makeInstance(const Document& doc, PanelId panel) {
    Instance instance{};
    for (PanelId other : doc.panels())
        if (!(other == panel) && instance.source == PanelId{}) instance.source = other;
    return instance;
}
OpData makeCurve(const Document&, PanelId) { return Curve{.edge = {}, .bulge = 2_cm}; }
OpData makeBezier(const Document&, PanelId) { return Bezier{.edge = {}, .startHandle = {0_cm, 0_cm}, .endHandle = {0_cm, 0_cm}}; }
OpData makeLine(const Document&, PanelId) { return Line{.from = {}, .angle = 45_deg}; }
OpData makeLineBetween(const Document&, PanelId) { return LineBetween{.from = {{}, 0.5}, .to = {{}, 0.5}}; }
OpData makeCut(const Document& doc, PanelId panel) {
    // Cut along the panel's most recent line, if it has one.
    Cut cut{};
    for (OpId op : doc.operations(panel))
        if (std::holds_alternative<Line>(doc.get(op))) cut.along = LineRef{op};
    return cut;
}
OpData makeMirror(const Document&, PanelId) { return Mirror{.across = {}}; }
OpData makeAdjust(const Document&, PanelId) { return AdjustPoint{.corner = {}, .dx = 0_cm, .dy = 0_cm}; }

const Offered offered[] = {
    {"Rectangle", "a width and a height", true, makeRect},
    {"Circle", "a radius; a sweep below 360 makes a slice", true, makeCircle},
    {"Ring sector", "for a circle skirt: inner length, sweep, depth", true, makeRing},
    {"Instance", "the finished outline of another panel", true, makeInstance},
    {"Curve", "bend an edge into an arc", false, makeCurve},
    {"Free curve", "shape an edge with two handles", false, makeBezier},
    {"Line", "between two points on the outline (easier with the Line tool)", false, makeLineBetween},
    {"Angled line", "an internal line from a corner at an angle", false, makeLine},
    {"Cut", "cut along a line and keep one side", false, makeCut},
    {"Mirror", "complete a half across one of its edges", false, makeMirror},
    {"Adjust point", "move one corner", false, makeAdjust},
};

} // namespace

App::App() {
    open(samples().front());
    // The avatar that comes with the project, if it can be found.
#ifdef CLOTHCAD_ASSETS_DIR
    if (loadAvatar(std::string(CLOTHCAD_ASSETS_DIR) + "/avatarV1.obj").empty()) return;
#endif
    loadAvatar("assets/avatarV1.obj");
}

std::string App::loadAvatar(const std::string& path) {
    const auto loaded = session_.loadAvatar(path);
    if (!loaded) return loaded.error();
    simulating_ = false;
    view3d_.fit();
    return {};
}

void App::useMannequin() {
    session_.useMannequin();
    simulating_ = false;
    view3d_.fit();
}

void App::open(const Sample& sample) { open(sample.build(), sample.name); }

void App::openEmpty() { open(Document(), "Untitled"); }

void App::open(Document document, const char* title) {
    arrangeInARow(document);
    session_.open(std::move(document));
    simulating_ = false;
    title_ = title;
    path_.clear();
    view3d_.fit();
    queued_.clear();
    picking_.reset();
    canvasDrag_.reset();
    cornerDrags_.clear();
    selectedCorners_.clear();
    turning_.reset();
    viewUntil_.reset();
    lengthEdit_.reset();
    canvas_.cancel();
    tool_ = Tool::Select;
    drag_.reset();
    typing_ = 0;
    naming_ = 0;
    rejected_.clear();
    notice_.clear();

    selected_.reset();
    for (PanelId panel : session_.document().panels())
        if (!selected_ && !session_.document().get(panel).construction) selected_ = panel;
    canvas_.fit();
}

// A document whose panels all sit at the table's origin has never been laid
// out: put its panels side by side. Done before it is opened, so it is not a step to undo.
void App::arrangeInARow(Document& doc) {
    const std::vector<PanelId> panels = doc.panels();
    if (panels.size() < 2) return;
    for (PanelId panel : panels)
        if (doc.get(panel).x != 0.0 || doc.get(panel).y != 0.0) return;
    const Result laidOut = evaluate(doc);
    double nextX = 0.0;
    for (PanelId panel : panels) {
        const std::optional<Bounds> box = bounds(laidOut[panel]);
        if (!box) continue;
        doc.place(panel, nextX - box->min.x, -box->min.y);
        nextX += box->max.x - box->min.x + 12_cm;
    }
}


void App::forgetHalfDone() {
    picking_.reset();
    canvasDrag_.reset();
    cornerDrags_.clear();
    selectedCorners_.clear();
    canvas_.cancel();
    notice_.clear();
    if (selected_ && !document().contains(*selected_)) selected_.reset();
}

void App::undo() {
    if (session_.editor().undo()) forgetHalfDone();
}

void App::redo() {
    if (session_.editor().redo()) forgetHalfDone();
}

bool App::change(bool newUndoStep, const std::function<void(Document&)>& verb) {
    const auto done = session_.editor().change(verb, {.step = newUndoStep ? session::Step::New : session::Step::Continue});
    if (!done) {
        notice_ = "Not changed: " + done.error();
        return false;
    }
    notice_.clear();
    return true;
}

void App::place(bool newUndoStep, const std::function<void(Document&)>& verb) {
    const auto done = session_.editor().change(verb, {.step = newUndoStep ? session::Step::New : session::Step::Continue,
                                                      .touches = session::Touches::PlacementOnly});
    if (!done) notice_ = "Not changed: " + done.error();
}

void App::runQueuedChanges() {
    if (queued_.empty()) return;
    notice_.clear();
    const std::vector<std::function<void(Document&)>> changes = std::move(queued_);
    queued_.clear();
    // All of them are one step to undo. Adding and removing things may leave
    // a problem behind: that is shown where it is, not refused.
    const auto done = session_.editor().change(
        [&](Document& doc) {
            for (const std::function<void(Document&)>& queued : changes) {
                try {
                    queued(doc);
                } catch (const std::exception& problem) {
                    notice_ = problem.what();
                }
            }
        },
        {.ifItBreaks = session::IfItBreaks::Allow});
    if (!done) notice_ = done.error();
    if (selected_ && !document().contains(*selected_)) selected_.reset();
}


const std::string* App::problemWith(Param parameter) const {
    for (const Error& error : shapes().errors)
        if (const Param* where = std::get_if<Param>(&error.where); where && *where == parameter) return &error.message;
    return nullptr;
}

const std::string* App::problemWith(OpId operation) const {
    for (const Error& error : shapes().errors)
        if (const OpId* where = std::get_if<OpId>(&error.where); where && *where == operation) return &error.message;
    return nullptr;
}

// ---- The frame --------------------------------------------------------------------

void App::frame() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin("Cloth CAD", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoBringToFrontOnFocus);

    menuBar();
    if (!ImGui::GetIO().WantTextInput && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z)) undo();
    if (!ImGui::GetIO().WantTextInput && (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Y) || ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z))) redo();
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S)) {
        if (path_.empty()) {
            fileAsk_ = FileAsk::SaveAs;
            openFilePopup_ = true;
        } else {
            saveTo(path_);
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape) && !ImGui::GetIO().WantTextInput) {
        // Esc backs out one step: a pick, then something half drawn, then the tool.
        if (picking_) picking_.reset();
        else if (!selectedCorners_.empty()) selectedCorners_.clear();
        else {
            canvas_.cancel();
            tool_ = Tool::Select;
        }
    }
    float& textScale = ImGui::GetStyle().FontScaleMain;
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Equal)) textScale = std::min(textScale + 0.1f, 2.5f);
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Minus)) textScale = std::max(textScale - 0.1f, 0.6f);
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_0)) textScale = 1.0f;

    // Pane widths follow the text size, so they grow with the display scale.
    const float em = ImGui::GetFontSize();
    const float left = 21.0f * em, right = 23.0f * em;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float middle = std::max(ImGui::GetContentRegionAvail().x - left - right - 2.0f * spacing, 100.0f);

    ImGui::BeginChild("parameters", ImVec2(left, 0.0f), ImGuiChildFlags_Borders);
    parametersPane();
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("canvas", ImVec2(middle, 0.0f), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    toolbar();
    // Looking back at an earlier step: show the document as of that step,
    // and let nothing be reshaped until the user returns to the present.
    if (viewUntil_ && (!selected_ || !document().contains(*viewUntil_) || !(document().panelOf(*viewUntil_) == *selected_)))
        viewUntil_.reset();
    const bool lookingBack = viewUntil_.has_value();
    if (lookingBack) {
        past_ = evaluateUpTo(document(), *viewUntil_);
        picking_.reset();
    }
    selectedCorners_.erase(std::remove_if(selectedCorners_.begin(), selectedCorners_.end(),
                                          [&](const CornerRef& c) { return !selected_ || !shapes()[*selected_].contains(c); }),
                           selectedCorners_.end());
    canvas_.setSelectedCorners(selectedCorners_);
    if (!session_.bodyReady()) simulating_ = false;
    session_.simulate(simulating_ && show3D_);
    session_.setBodyPresent(showBody_);
    session_.setParticleDistance(particleDistance_);
    if (const std::optional<std::string> problem = session_.update((showMesh_ || show3D_) && !lookingBack)) {
        notice_ = *problem;
        simulating_ = false;
    }
    canvas_.setMesh(showMesh_ && !lookingBack ? session_.mesh() : nullptr);

    // The 3D view on the left, the table on the right.
    if (show3D_) {
        ImGui::BeginChild("space", ImVec2(ImGui::GetContentRegionAvail().x * 0.42f, 0.0f), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::BeginDisabled(!bodyReady());
        if (ImGui::Button(simulating_ ? "Pause" : session_.clothShown() ? "Continue" : "Simulate")) simulating_ = !simulating_;
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("Let the cloth fall: gravity, seams pulling closed, and the body in the way.\nIt stays draped when you change the pattern. A panel you place somewhere else starts again from there.");
        ImGui::SameLine();
        ImGui::BeginDisabled(!session_.clothShown());
        if (ImGui::Button("Reset")) {
            session_.resetCloth();
            simulating_ = false;
        }
        ImGui::SetItemTooltip("Put the cloth back where the panels are arranged.");
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (!bodyReady()) ImGui::TextDisabled("preparing the avatar...");
        else if (session_.clothShown()) ImGui::TextDisabled(session_.cloth().sewing ? "%.1f s, sewing   (%.1f ms a step)" : "%.1f s   (%.1f ms a step)", session_.cloth().time, session_.cloth().stepMilliseconds);
        else ImGui::TextDisabled("arranged, not simulated");
        const View3DEvents seen = view3d_.draw(document(), session_.shownMesh(), session_.shownPositions(), showBody_ ? &session_.bodySurface() : nullptr, selected_, showMesh_, session_.sceneVersion());
        if (seen.clicked && seen.panel != selected_) {
            selected_ = seen.panel;
            selectedCorners_.clear();
        }
        if (seen.carried && document().contains(seen.carried->panel)) {
            // Carrying a panel through space only places it: nothing to recompute.
            const CarryPanel& carried = *seen.carried;
            if (carried.phase == DragPhase::Begin) {
                carriedFrom_.reset();
                if (const sim::PanelMesh* panelMesh = session_.mesh()->find(carried.panel)) carriedFrom_ = sim::arrangementOf(document(), *panelMesh);
            }
            if (carriedFrom_) {
                Arrangement now = *carriedFrom_;
                now.placed = true;
                now.x += carried.by.x;
                now.y += carried.by.y;
                now.z += carried.by.z;
                place(carried.phase == DragPhase::Begin, [&](Document& doc) { doc.arrange(carried.panel, now); });
            }
            if (carried.phase == DragPhase::End) carriedFrom_.reset();
        }
        ImGui::EndChild();
        ImGui::SameLine();
    }
    ImGui::BeginChild("table", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const std::optional<PickRequest> request = pickRequest();
    const CanvasEvents events = canvas_.draw(document(), lookingBack ? past_ : shapes(), selected_, tool_,
                                             request ? &*request : nullptr, lookingBack);
    if (events.gesture) handle(*events.gesture);
    if (events.picked && picking_) {
        const Picking chosen = *picking_;
        const Reference value = *events.picked;
        later([chosen, value](Document& doc) { doc.set(chosen.operation, chosen.reference, value); });
        picking_.reset();
    }
    ImGui::EndChild();  // table
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("details", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
    detailsPane();
    ImGui::EndChild();

    lengthPopup();
    filePopup();
    ImGui::End();
    runQueuedChanges();
}

// ---- The toolbar above the canvas ---------------------------------------------------------

void App::toolbar() {
    struct Entry {
        Tool tool;
        const char* name;
        const char* help;
    };
    static const Entry tools[] = {
        {Tool::Select, "Select", "Click a panel to select it. Drag a panel to move it.\nDrag a corner or a curve handle to reshape it."},
        {Tool::Rectangle, "Rectangle", "Drag on the table to draw a new rectangular panel."},
        {Tool::Circle, "Circle", "Press where the centre goes and drag out to the radius.\nA sweep below 360 in the details makes it a slice."},
        {Tool::RingSector, "Ring", "Click on the table to place a new ring-shaped panel, as for a circle skirt."},
        {Tool::Curve, "Curve", "Click an edge to make it a free curve. Then drag its two handles."},
        {Tool::Line, "Line", "Draw an internal line: click two points on one panel's outline."},
        {Tool::Cut, "Cut", "Click the part of a panel to remove, on one side of an internal line."},
        {Tool::Split, "Split", "Click an internal line to split its panel into two panels."},
        {Tool::Mirror, "Mirror", "Click a straight edge to complete the panel across it."},
        {Tool::Sew, "Sew", "Click two edges to sew them together, on one panel or two.\nThe seam follows the edges when the shapes change."},
    };
    const float gap = ImGui::GetStyle().ItemInnerSpacing.x;
    for (const Entry& entry : tools) {
        const bool active = tool_ == entry.tool && !picking_;
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::Button(entry.name)) {
            tool_ = entry.tool;
            picking_.reset();
            canvas_.cancel();
        }
        if (active) ImGui::PopStyleColor();
        ImGui::SetItemTooltip("%s", entry.help);
        ImGui::SameLine(0.0f, gap);
    }

    // Things to do with the selected panel.
    ImGui::SameLine(0.0f, 4.0f * gap);
    ImGui::BeginDisabled(!selected_);
    if (ImGui::Button("Copy")) addBeside(*selected_, Follow::Copy);
    ImGui::SetItemTooltip("A separate panel with the same operations.\nIts formulas still refer to the same parameters.");
    ImGui::SameLine(0.0f, gap);
    if (ImGui::Button("Instance")) addBeside(*selected_, Follow::Instance);
    ImGui::SetItemTooltip("A panel that follows the selected one entirely.");
    ImGui::SameLine(0.0f, gap);
    if (ImGui::Button("Mirrored instance")) addBeside(*selected_, Follow::MirroredInstance);
    ImGui::SetItemTooltip("A panel that follows the selected one, flipped left to right.");
    ImGui::SameLine(0.0f, gap);
    if (ImGui::Button("Bake")) later([panel = *selected_](Document& doc) { bake(doc, panel); });
    ImGui::SetItemTooltip("Replace every formula in the selected panel with the number it comes to now,\nso the panel no longer follows any parameter. Useful on a copy.");
    ImGui::SameLine(0.0f, gap);
    if (ImGui::Button("Delete")) later([panel = *selected_](Document& doc) { doc.remove(panel); });
    ImGui::SetItemTooltip("Remove the selected panel.");
    ImGui::EndDisabled();
    ImGui::SameLine(0.0f, 4.0f * gap);
    if (ImGui::Button("Fit")) {
        canvas_.fit();
        view3d_.fit();
    }
    ImGui::SetItemTooltip("Zoom to show every panel.");
}

void App::addBeside(PanelId source, Follow how) {
    // Just to the right of the source's outline.
    const Panel& from = document().get(source);
    double x = from.x + 30_cm, y = from.y;
    if (const std::optional<Bounds> box = bounds(shapes()[source])) {
        const double newLeft = how == Follow::MirroredInstance ? -box->max.x : box->min.x;  // the new panel's own left edge
        const std::optional<Bounds> placed = bounds(turned(shapes()[source], from.angle));  // where the source really is
        x = from.x + (placed ? placed->max.x : box->max.x) + 12_cm - newLeft;
    }
    later([this, source, how, x, y](Document& doc) {
        const char* suffix = how == Follow::Copy ? " copy" : how == Follow::Instance ? " instance" : " mirrored";
        const std::string name = firstFree(doc.get(source).name + suffix, [&](const std::string& n) { return panelNamed(doc, n); });
        if (how == Follow::Copy) {
            selected_ = doc.copy(source, Panel{.name = name, .x = x, .y = y});
        } else {
            const PanelId added = doc.add(Panel{.name = name, .x = x, .y = y});
            doc.add(added, Instance{.source = source, .mirrored = how == Follow::MirroredInstance});
            selected_ = added;
        }
    });
}

// ---- Canvas gestures ------------------------------------------------------------------------

void App::handle(const Gesture& gesture) {
    std::visit([this](const auto& g) { on(g); }, gesture);
}

void App::on(const SelectPanel& g) {
    if (g.panel != selected_) selectedCorners_.clear();
    selected_ = g.panel;
}

void App::on(const SelectCorners& g) {
    if (!document().contains(g.panel)) return;
    if (selected_ != g.panel) selectedCorners_.clear();
    selected_ = g.panel;
    const auto has = [&](const CornerRef& c) { return std::find(selectedCorners_.begin(), selectedCorners_.end(), c) != selectedCorners_.end(); };
    if (!g.add) {
        selectedCorners_ = g.corners;
    } else if (g.corners.size() == 1 && has(g.corners.front())) {
        selectedCorners_.erase(std::find(selectedCorners_.begin(), selectedCorners_.end(), g.corners.front()));
    } else {
        for (const CornerRef& c : g.corners)
            if (!has(c)) selectedCorners_.push_back(c);
    }
}

void App::on(const SewEdges& g) {
    if (!document().contains(g.a.panel) || !document().contains(g.b.panel)) return;
    for (SeamId id : document().seams()) {
        const Seam& seam = document().get(id);
        if ((seam.a == g.a && seam.b == g.b) || (seam.a == g.b && seam.b == g.a)) {
            notice_ = "These two edges are already sewn together.";
            return;
        }
    }
    later([g](Document& doc) { doc.add(Seam{.a = g.a, .b = g.b}); });
}

void App::on(const RotatePanel& g) {
    if (!document().contains(g.panel)) return;
    const Panel& panel = document().get(g.panel);
    if (g.phase == DragPhase::Begin) {
        const std::optional<Bounds> own = bounds(shapes()[g.panel]);
        const Vec2 centre = own ? Vec2{(own->min.x + own->max.x) / 2.0, (own->min.y + own->max.y) / 2.0} : Vec2{};
        const Vec2 now = turned(centre, panel.angle);
        turning_ = Turning{centre, {panel.x + now.x, panel.y + now.y}};
    }
    if (!turning_) return;
    const Vec2 shifted = turned(turning_->centre, g.angle);
    place(g.phase == DragPhase::Begin, [&](Document& doc) {
        doc.place(g.panel, turning_->pivot.x - shifted.x, turning_->pivot.y - shifted.y);
        doc.turn(g.panel, g.angle);
    });
    if (g.phase == DragPhase::End) turning_.reset();
}

void App::on(const MovePanel& g) {
    if (!document().contains(g.panel)) return;
    place(g.phase == DragPhase::Begin, [&](Document& doc) { doc.place(g.panel, g.x, g.y); });
}

namespace {

// On a symmetric panel a drag has to respect the mirror: a drag on the
// mirrored side is the mirror image of the drag on the drawn side, and a
// point lying on the mirror's axis may only slide along it.
struct Movement {
    double dx, dy;
};
Movement acrossTheMirror(const Document& doc, const Result& shapes, PanelId panel, Movement moved, bool wasImage, const Vec2* position) {
    const std::optional<OpRef<Mirror>> mirror = symmetryOf(doc, panel);
    if (!mirror) return moved;
    for (const SymmetryAxis& axis : shapes[panel].axes) {
        if (!(axis.mirror == OpId(*mirror))) continue;
        const double length = std::hypot(axis.end.x - axis.start.x, axis.end.y - axis.start.y);
        if (length < 1e-9) continue;
        const double ux = (axis.end.x - axis.start.x) / length, uy = (axis.end.y - axis.start.y) / length;
        const double along = moved.dx * ux + moved.dy * uy;
        if (wasImage) moved = {2.0 * along * ux - moved.dx, 2.0 * along * uy - moved.dy};
        if (position) {
            const double off = (position->x - axis.start.x) * uy - (position->y - axis.start.y) * ux;
            if (std::abs(off) < 1e-6) moved = {along * ux, along * uy};  // on the axis: slide along it
        }
    }
    return moved;
}

} // namespace

void App::on(const MoveCorner& image) {
    bool newStep = false;  // whether this change opens the drag's undo step
    if (!document().contains(image.panel)) return;
    const Vec2 own = turned(Vec2{image.dx, image.dy}, -document().get(image.panel).angle);  // the drag in the panel's own coordinates
    const Pattern& shape = shapes()[image.panel];
    const auto isSelected = [&](const CornerRef& c) { return std::find(selectedCorners_.begin(), selectedCorners_.end(), c) != selectedCorners_.end(); };

    // Dragging a selected corner moves the whole selection; dragging any
    // other corner selects just that one.
    if (image.phase == DragPhase::Begin && !(selectedCorners_.size() > 1 && isSelected(image.corner))) selectedCorners_ = {image.corner};
    std::vector<CornerRef> grabbed = selectedCorners_.size() > 1 && isSelected(image.corner) ? selectedCorners_ : std::vector<CornerRef>{image.corner};

    // On a symmetric panel, a mirrored corner moves its original, the mirrored
    // way; a corner and its own image count once.
    struct Moving {
        CornerRef corner;
        Movement by;
    };
    std::vector<Moving> moving;
    for (const CornerRef& c : grabbed) {
        if (!shape.contains(c)) continue;
        const DrawnCorner drawn = drawnSide(document(), image.panel, c);
        if (std::any_of(moving.begin(), moving.end(), [&](const Moving& m) { return m.corner == drawn.corner; })) continue;
        const Vec2 where = shape.position(c);
        moving.push_back({drawn.corner, acrossTheMirror(document(), shapes(), image.panel, {own.x, own.y}, drawn.wasImage, &where)});
    }

    if (image.phase == DragPhase::Begin) {
        canvasDrag_.reset();
        cornerDrags_.clear();
        // Corners that come from a formula stay where they are.
        moving.erase(std::remove_if(moving.begin(), moving.end(), [&](const Moving& m) { return !cornerLock(document(), image.panel, m.corner).empty(); }),
                     moving.end());
        if (moving.empty()) return;
        if (moving.size() == 1) {
            // Keep dragging the same corner in one operation: if the panel's last
            // operation already moves this corner by plain numbers, continue it.
            const CornerRef corner = moving.front().corner;
            const std::vector<OpId> operations = document().operations(image.panel);
            const AdjustPoint* last = operations.empty() ? nullptr : std::get_if<AdjustPoint>(&document().get(operations.back()));
            if (last && last->corner == corner && asNumber(last->dx) && asNumber(last->dy)) {
                newStep = true;  // the drag's first change opens its undo step
                canvasDrag_ = CanvasDrag{operations.back(), false, *asNumber(last->dx), *asNumber(last->dy)};
            } else {
                OpId added;
                const bool accepted = change(true, [&](Document& doc) {
                    added = doc.add(image.panel, AdjustPoint{.corner = corner, .dx = 0_cm, .dy = 0_cm});
                });
                if (!accepted) return;  // for instance the only point of a whole circle
                canvasDrag_ = CanvasDrag{added, false, 0.0, 0.0};
            }
        } else {
            // A group: one adjustment each, all in one undo step.
            std::vector<CornerDrag> started;
            const bool accepted = change(true, [&](Document& doc) {
                for (const Moving& m : moving)
                    started.push_back({m.corner, doc.add(image.panel, AdjustPoint{.corner = m.corner, .dx = 0_cm, .dy = 0_cm}).id});
            });
            if (!accepted) return;
            cornerDrags_ = std::move(started);
        }
    }

    if (!cornerDrags_.empty()) {
        change(false, [&](Document& doc) {
            for (const CornerDrag& d : cornerDrags_) {
                const auto it = std::find_if(moving.begin(), moving.end(), [&](const Moving& m) { return m.corner == d.corner; });
                if (it == moving.end() || !doc.contains(d.operation)) continue;
                const OpRef<AdjustPoint> adjust(d.operation);
                doc.set(adjust, &AdjustPoint::dx, it->by.dx);
                doc.set(adjust, &AdjustPoint::dy, it->by.dy);
            }
        });
        if (image.phase == DragPhase::End) cornerDrags_.clear();
        return;
    }
    if (!canvasDrag_ || !document().contains(canvasDrag_->operation) || moving.empty()) return;
    const OpRef<AdjustPoint> adjust(canvasDrag_->operation);
    const double x = canvasDrag_->baseX + moving.front().by.dx, y = canvasDrag_->baseY + moving.front().by.dy;
    change(newStep, [&](Document& doc) {
        doc.set(adjust, &AdjustPoint::dx, x);
        doc.set(adjust, &AdjustPoint::dy, y);
    });
    if (image.phase == DragPhase::End) canvasDrag_.reset();
}

void App::on(const MoveHandle& image) {
    if (!document().contains(image.panel)) return;
    // On a symmetric panel, a handle on the mirrored side is the opposite
    // handle of the original edge, moved the mirrored way.
    const DrawnEdge drawn = drawnSide(document(), image.panel, image.edge);
    const Vec2 own = turned(Vec2{image.dx, image.dy}, -document().get(image.panel).angle);  // the drag in the panel's own coordinates
    const Movement moved = acrossTheMirror(document(), shapes(), image.panel, {own.x, own.y}, drawn.wasImage, nullptr);
    const MoveHandle g{image.panel, drawn.edge, drawn.wasImage ? !image.endHandle : image.endHandle, moved.dx, moved.dy, image.phase};

    if (g.phase == DragPhase::Begin) {
        canvasDrag_.reset();
        if (!handleLock(document(), g.panel, g.edge, g.endHandle).empty()) return;
        const OpRef<Bezier> curve = *curveOf(document(), g.panel, g.edge);
        const Offset& handle = g.endHandle ? document().get(curve).endHandle : document().get(curve).startHandle;
        canvasDrag_ = CanvasDrag{curve, g.endHandle, *asNumber(handle.x), *asNumber(handle.y)};
    }
    if (!canvasDrag_ || !document().contains(canvasDrag_->operation)) return;
    const OpRef<Bezier> curve(canvasDrag_->operation);
    const Offset handle{canvasDrag_->baseX + g.dx, canvasDrag_->baseY + g.dy};
    change(g.phase == DragPhase::Begin, [&](Document& doc) { doc.set(curve, canvasDrag_->endHandle ? &Bezier::endHandle : &Bezier::startHandle, handle); });
    if (g.phase == DragPhase::End) canvasDrag_.reset();
}

void App::on(const DrawRect& g) {
    later([this, g](Document& doc) {
        const std::string name = firstFree("panel", [&](const std::string& n) { return panelNamed(doc, n); });
        const PanelId panel = doc.add(Panel{.name = name, .x = g.x, .y = g.y});
        doc.add(panel, Rect{.width = g.width, .height = g.height});
        selected_ = panel;
    });
    tool_ = Tool::Select;
}

void App::on(const DrawCircle& g) {
    later([this, g](Document& doc) {
        const std::string name = firstFree("circle", [&](const std::string& n) { return panelNamed(doc, n); });
        const PanelId panel = doc.add(Panel{.name = name, .x = g.x, .y = g.y});
        doc.add(panel, Circle{.radius = g.radius});
        selected_ = panel;
    });
    tool_ = Tool::Select;
}

void App::on(const PlaceRing& g) {
    later([this, g](Document& doc) {
        const std::string name = firstFree("skirt", [&](const std::string& n) { return panelNamed(doc, n); });
        const PanelId panel = doc.add(Panel{.name = name, .x = g.x, .y = g.y});
        doc.add(panel, RingSector{.innerLength = 70_cm, .sweep = 180_deg, .depth = 60_cm});
        selected_ = panel;
    });
    tool_ = Tool::Select;
}

void App::on(const BendEdge& image) {
    // On a symmetric panel, bending a mirrored edge bends its original: both follow.
    const BendEdge g{image.panel, drawnSide(document(), image.panel, image.edge).edge};
    selected_ = g.panel;
    tool_ = Tool::Select;  // so the handles can be dragged straight away
    if (curveOf(document(), g.panel, g.edge)) return;  // already a free curve: its handles are showing now
    const Pattern& shape = shapes()[g.panel];
    if (!shape.contains(g.edge)) return;
    // Handles a third of the way along the edge: it starts out straight.
    const Edge& edge = shape.edge(g.edge);
    const Vec2 start = shape.vertices[edge.start].position, end = shape.vertices[edge.end].position;
    double x = std::round((end.x - start.x) / 3.0 * 10.0) / 10.0, y = std::round((end.y - start.y) / 3.0 * 10.0) / 10.0;
    double endX = -x, endY = -y;
    if (edge.arc) {
        // An arc becomes a free curve that follows it, so bending it starts from what is drawn.
        const std::optional<CubicCurve> follow = cubicFromArc(shape, edge);
        if (!follow) {
            notice_ = "A whole circle has no ends to bend between. Make it a slice, or cut it first.";
            return;
        }
        const auto tenth = [](double v) { return std::round(v * 10.0) / 10.0; };
        x = tenth(follow->control1.x - start.x), y = tenth(follow->control1.y - start.y);
        endX = tenth(follow->control2.x - end.x), endY = tenth(follow->control2.y - end.y);
    }
    later([g, x, y, endX, endY](Document& doc) {
        doc.add(g.panel, Bezier{.edge = g.edge, .startHandle = {x, y}, .endHandle = {endX, endY}});
    });
}

void App::on(const DrawLine& g) {
    selected_ = g.panel;
    // On a symmetric panel the line is drawn on the half that was drawn. An
    // end placed on the mirrored side is moved to the same place on the original edge.
    const DrawnEdge from = drawnSide(document(), g.panel, g.from.edge), to = drawnSide(document(), g.panel, g.to.edge);
    if (from.wasImage != to.wasImage) {
        notice_ = "A line can't cross the mirror of a symmetric panel. Draw it on one half, or switch off \"Applied last\" on the mirror.";
        return;
    }
    const double fromAlong = from.wasImage ? 1.0 - g.from.along : g.from.along;
    const double toAlong = to.wasImage ? 1.0 - g.to.along : g.to.along;
    later([panel = g.panel, from, to, fromAlong, toAlong](Document& doc) {
        doc.add(panel, LineBetween{.from = {from.edge, fromAlong}, .to = {to.edge, toAlong}});
    });
}

// A double-click on a length: find the setting that length is, and open it for typing.
void App::on(const EditLength& g) {
    selected_ = g.panel;
    const Pattern& shape = shapes()[g.panel];
    const EdgeRef edge = g.edge;
    if (document().contains(edge.op) && shape.contains(edge)) {
        const OpData& data = document().get(edge.op);
        for (const Dimension& dimension : dimensions(data, edge.op)) {
            if (dimension.edge.role != edge.role) continue;  // a mirrored copy shares the role
            const Field field = fields(data)[dimension.field.index];
            const std::optional<double> value = valueOf(field.value, shapes());
            if (!value || std::abs(*value - shape.length(edge)) > 1e-6) continue;  // a later step changed the edge
            lengthEdit_ = LengthEdit{edge.op, dimension.field, std::string(field.label) + " of " + nameOf(Reference(edge), document())};
            std::snprintf(lengthText_, sizeof lengthText_, "%s", formatExpr(field.value, document()).c_str());
            lengthProblem_.clear();
            openLengthPopup_ = true;
            return;
        }
    }
    notice_ = "This edge's length is not a setting of its own; it follows from where its corners are. Drag its corners instead.";
}

void App::lengthPopup() {
    if (openLengthPopup_) {
        ImGui::OpenPopup("edge length");
        openLengthPopup_ = false;
    }
    if (!ImGui::BeginPopup("edge length")) return;
    if (!lengthEdit_ || !document().contains(lengthEdit_->operation)) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    ImGui::TextDisabled("%s", lengthEdit_->label.c_str());
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(14.0f * ImGui::GetFontSize());
    if (ImGui::InputText("##length", lengthText_, sizeof lengthText_,
                         ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
        const std::variant<Expr, TextError> parsed = parseExpr(lengthText_, document());
        if (const Expr* expr = std::get_if<Expr>(&parsed)) {
            const LengthEdit edit = *lengthEdit_;
            const bool accepted = change(true, [&](Document& doc) { doc.set(edit.operation, edit.field, *expr); });
            if (accepted) ImGui::CloseCurrentPopup();
            else lengthProblem_ = notice_;
        } else {
            lengthProblem_ = std::get<TextError>(parsed).message;
        }
        if (!lengthProblem_.empty()) ImGui::SetKeyboardFocusHere(-1);  // stay in the field to correct it
    }
    ImGui::TextDisabled("A number or a formula. Enter applies, Esc cancels.");
    if (!lengthProblem_.empty()) errorText(lengthProblem_);
    ImGui::EndPopup();
}

void App::on(const CutAway& g) {
    selected_ = g.panel;
    later([g](Document& doc) { doc.add(g.panel, Cut{.along = g.line, .keep = g.keep}); });
}

void App::on(const SplitAt& g) {
    selected_ = g.panel;
    later([g](Document& doc) {
        // One panel keeps one side; a new panel is what the cut removes.
        const OpRef<Cut> cut = doc.add(g.panel, Cut{.along = g.line, .keep = Cut::Side::Left});
        const Panel& source = doc.get(g.panel);
        const std::string name = firstFree(source.name + " part", [&](const std::string& n) { return panelNamed(doc, n); });
        const PanelId other = doc.add(Panel{.name = name, .x = source.x, .y = source.y});
        doc.add(other, Offcut{.of = cut});
    });
    tool_ = Tool::Select;
}

void App::on(const MirrorAcross& g) {
    selected_ = g.panel;
    later([g](Document& doc) { doc.add(g.panel, Mirror{.across = g.edge}); });
    tool_ = Tool::Select;
}

// What to offer on the canvas for the reference being chosen, if any.
std::optional<PickRequest> App::pickRequest() {
    if (!picking_) return std::nullopt;
    if (!document().contains(picking_->operation)) {
        picking_.reset();
        return std::nullopt;
    }
    const std::vector<ReferenceField> slots = references(document().get(picking_->operation));
    if (picking_->reference.index >= slots.size() || std::holds_alternative<PanelId>(slots[picking_->reference.index].value)) {
        picking_.reset();
        return std::nullopt;
    }
    const Reference& slot = slots[picking_->reference.index].value;
    const PickRequest::Kind kind = std::holds_alternative<EdgeRef>(slot)     ? PickRequest::Kind::Edge
                                   : std::holds_alternative<CornerRef>(slot) ? PickRequest::Kind::Corner
                                                                             : PickRequest::Kind::Line;
    // Only what exists just before the operation can be referred to by it.
    return PickRequest{document().panelOf(picking_->operation), kind, outlineBefore(document(), picking_->operation)};
}

void App::menuBar() {
    if (!ImGui::BeginMenuBar()) return;
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New, empty")) openEmpty();
        if (ImGui::MenuItem("Open...")) {
            fileAsk_ = FileAsk::Open;
            openFilePopup_ = true;
        }
        if (ImGui::MenuItem("Save", "Ctrl+S")) {
            if (path_.empty()) {
                fileAsk_ = FileAsk::SaveAs;
                openFilePopup_ = true;
            } else {
                saveTo(path_);
            }
        }
        if (ImGui::MenuItem("Save as...")) {
            fileAsk_ = FileAsk::SaveAs;
            openFilePopup_ = true;
        }
        ImGui::SeparatorText("Body");
        if (ImGui::MenuItem("Load avatar (OBJ)...")) {
            fileAsk_ = FileAsk::Avatar;
            openFilePopup_ = true;
        }
        ImGui::SetItemTooltip("A Wavefront OBJ file in cm, y up, standing on the floor and facing +z.\nIt should be one closed surface.");
        if (ImGui::MenuItem("Use the mannequin", nullptr, session_.avatarPath().empty())) useMannequin();
        ImGui::SeparatorText("Examples");
        for (const Sample& sample : samples())
            if (ImGui::MenuItem(sample.name)) open(sample);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        if (ImGui::MenuItem("Undo", "Ctrl+Z", false, session_.editor().canUndo())) undo();
        if (ImGui::MenuItem("Redo", "Ctrl+Y", false, session_.editor().canRedo())) redo();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        if (ImGui::MenuItem("Fit all panels")) {
            canvas_.fit();
            view3d_.fit();
        }
        ImGui::MenuItem("3D view", nullptr, &show3D_);
        ImGui::MenuItem("Body", nullptr, &showBody_);
        ImGui::SetItemTooltip("Hidden, the body is also taken out of the simulation.");
        if (ImGui::MenuItem("Grid", nullptr, canvas_.gridVisible())) canvas_.setGridVisible(!canvas_.gridVisible());
        ImGui::Separator();
        ImGui::MenuItem("Cloth mesh", nullptr, &showMesh_);
        ImGui::SetItemTooltip("Show the panels as the triangles they will be simulated with, on the table and in 3D.");
        ImGui::SetNextItemWidth(8.0f * ImGui::GetFontSize());
        ImGui::SliderFloat("Particle distance", &particleDistance_, 0.5f, 5.0f, "%.1f cm");
        if (showMesh_) {
            std::size_t particles = 0, triangles = 0;
            for (const sim::PanelMesh& mesh : session_.mesh()->panels) {
                particles += mesh.points.size();
                triangles += mesh.triangles.size();
            }
            ImGui::TextDisabled("%zu particles, %zu triangles, %zu stitches", particles, triangles, session_.mesh()->stitches.size());
        }
        ImGui::Separator();
        float& textScale = ImGui::GetStyle().FontScaleMain;
        if (ImGui::MenuItem("Larger text", "Ctrl +")) textScale = std::min(textScale + 0.1f, 2.5f);
        if (ImGui::MenuItem("Smaller text", "Ctrl -")) textScale = std::max(textScale - 0.1f, 0.6f);
        if (ImGui::MenuItem("Normal text size", "Ctrl 0")) textScale = 1.0f;
        ImGui::EndMenu();
    }
    ImGui::Separator();
    ImGui::TextUnformatted(title_.c_str());
    if (!notice_.empty()) {
        ImGui::Separator();
        ImGui::TextColored(color::error, "%s", notice_.c_str());
    }
    if (!shapes().errors.empty()) {
        ImGui::Separator();
        ImGui::TextColored(color::error, "%d problem%s", static_cast<int>(shapes().errors.size()),
                           shapes().errors.size() == 1 ? "" : "s");
    }
    ImGui::EndMenuBar();
}

void App::saveTo(const std::string& path) {
    const std::string problem = save(document(), path);
    if (!problem.empty()) {
        notice_ = "Not saved: " + problem;
        fileProblem_ = problem;
        return;
    }
    path_ = path;
    title_ = path;
    fileProblem_.clear();
    notice_.clear();
}

bool App::openFrom(const std::string& path) {
    std::variant<Document, FileError> read = clothcad::open(path);
    if (const FileError* error = std::get_if<FileError>(&read)) {
        fileProblem_ = error->line > 0 ? "line " + std::to_string(error->line) + ": " + error->message : error->message;
        return false;
    }
    open(std::move(std::get<Document>(read)), "");
    path_ = path;
    title_ = path;
    fileProblem_.clear();
    return true;
}

// Asks for a file name to open or to save as. A plain path, relative to the
// folder the program was started in.
void App::filePopup() {
    if (openFilePopup_) {
        ImGui::OpenPopup("file");
        openFilePopup_ = false;
        fileProblem_.clear();
        if (fileAsk_ == FileAsk::Avatar) std::snprintf(pathText_, sizeof pathText_, "%s", session_.avatarPath().empty() ? "assets/avatarV1.obj" : session_.avatarPath().c_str());
        else if (!path_.empty()) std::snprintf(pathText_, sizeof pathText_, "%s", path_.c_str());
        else if (std::string(pathText_).ends_with(".obj")) std::snprintf(pathText_, sizeof pathText_, "pattern.clothcad");
    }
    if (!ImGui::BeginPopupModal("file", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) return;
    const bool opening = fileAsk_ == FileAsk::Open;
    const bool avatar = fileAsk_ == FileAsk::Avatar;
    ImGui::TextUnformatted(avatar ? "Load which avatar (OBJ file)?" : opening ? "Open which file?" : "Save as which file?");
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(32.0f * ImGui::GetFontSize());
    bool go = ImGui::InputText("##path", pathText_, sizeof pathText_, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::TextDisabled("A file name, or a full path. A plain name is in the folder the program runs in.");
    if (!fileProblem_.empty()) errorText(fileProblem_);
    go = ImGui::Button(avatar ? "Load" : opening ? "Open" : "Save") || go;
    ImGui::SameLine();
    const bool cancel = ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape);
    if (go && pathText_[0] != '\0') {
        const std::string path = pathText_;
        if (avatar) {
            fileProblem_ = loadAvatar(path);
            if (fileProblem_.empty()) ImGui::CloseCurrentPopup();
        } else if (opening) {
            if (openFrom(path)) ImGui::CloseCurrentPopup();
        } else {
            saveTo(path);
            if (fileProblem_.empty()) ImGui::CloseCurrentPopup();
        }
    } else if (cancel) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// ---- Text fields -----------------------------------------------------------------------

std::optional<App::FieldEdit> App::formulaField(const Expr& current, const Limits* limits, double step) {
    const unsigned int field = ImGui::GetID("##formula");
    const float width = ImGui::GetContentRegionAvail().x;
    if (typing_ == field) return typingField(width);

    // ---- Closed: shows the formula; drag sideways to change a number, click to type ----
    const std::optional<double> number = asNumber(current);
    const std::string text = formatExpr(current, document());
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 corner = ImGui::GetCursorScreenPos();
    const ImVec2 size{width, ImGui::GetFrameHeight()};
    const ImVec2 far{corner.x + size.x, corner.y + size.y};

    ImGui::InvisibleButton("##formula", size);
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();
    const bool released = ImGui::IsItemDeactivated();

    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(corner, far,
                        ImGui::GetColorU32(held ? ImGuiCol_FrameBgActive : hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg),
                        style.FrameRounding);
    draw->PushClipRect(corner, far, true);
    const ImVec2 textAt{corner.x + style.FramePadding.x, corner.y + style.FramePadding.y};
    // A formula is shown in the "driven" colour, with what it comes to beside it.
    draw->AddText(textAt, number ? ImGui::GetColorU32(ImGuiCol_Text) : ImGui::GetColorU32(color::driven), text.c_str());
    if (!number)
        if (const std::optional<double> value = valueOf(current, shapes())) {
            char result[32];
            std::snprintf(result, sizeof result, "= %.5g", *value);
            const float resultWidth = ImGui::CalcTextSize(result).x;
            const float textEnd = textAt.x + ImGui::CalcTextSize(text.c_str()).x + style.ItemSpacing.x;
            const float x = std::max(far.x - style.FramePadding.x - resultWidth, textEnd);
            draw->AddText({x, textAt.y}, ImGui::GetColorU32(ImGuiCol_TextDisabled), result);
        }
    draw->PopClipRect();

    if (hovered || held) {
        if (number) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        if (!held) {
            std::string help = number ? "Drag left or right to change. Click to type.\nShift: faster. Alt: finer."
                                      : "Click to edit the formula.";
            if (limits && !limits->rule().empty()) help += "\nThe value " + limits->rule() + ".";
            ImGui::SetItemTooltip("%s", help.c_str());
        }
    }

    std::optional<FieldEdit> edit;
    if (held && number) {
        const float pulled = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 3.0f * pixel()).x;  // right is more
        const bool dragging = drag_ && drag_->field == field;
        if (pulled != 0.0f && !dragging)
            drag_ = Drag{field, *number, step > 0.0 ? step : std::abs(*number) < 5.0 ? 0.01 : 0.1};  // small numbers move in finer steps
        if (drag_ && drag_->field == field) {
            const ImGuiIO& io = ImGui::GetIO();
            const double stepNow = drag_->step * (io.KeyShift ? 10.0 : io.KeyAlt ? 0.1 : 1.0);
            const double pixels = std::round(pulled / pixel());
            const double value = std::round((drag_->start + pixels * stepNow) / (drag_->step * 0.1)) * (drag_->step * 0.1);
            if (value != *number) edit = FieldEdit{Expr(value), drag_->changed};
        }
    }
    if (released) {
        const bool wasDrag = drag_ && drag_->field == field;
        if (wasDrag) drag_.reset();
        else {
            // A click without a drag: open the field for typing.
            typing_ = field;
            focusTyping_ = true;
            rejected_.clear();
            std::snprintf(typed_, sizeof typed_, "%s", text.c_str());
        }
    }
    return edit;
}

std::optional<App::FieldEdit> App::typingField(float width) {
    if (focusTyping_) {
        ImGui::SetKeyboardFocusHere();
        focusTyping_ = false;
    }
    ImGui::SetNextItemWidth(width);
    const bool entered = ImGui::InputText("##formula", typed_, sizeof typed_,
                                          ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
    const bool left = ImGui::IsItemDeactivated();

    std::optional<FieldEdit> edit;
    if (entered) {
        const std::variant<Expr, TextError> parsed = parseExpr(typed_, document());
        if (const Expr* expr = std::get_if<Expr>(&parsed)) {
            edit = FieldEdit{*expr, false};
            typing_ = 0;
            rejected_.clear();
        } else {
            // Stay open so the text can be corrected.
            rejected_ = std::get<TextError>(parsed).message;
            focusTyping_ = true;
            ignoreBlurUntilFrame_ = ImGui::GetFrameCount() + 2;
        }
    } else if (left && ImGui::GetFrameCount() > ignoreBlurUntilFrame_) {
        // Escape, or a click somewhere else: leave the value as it was.
        typing_ = 0;
        rejected_.clear();
    }
    if (typing_ != 0 && !rejected_.empty()) errorText(rejected_);
    return edit;
}

std::optional<std::string> App::nameField(const std::string& current) {
    const unsigned int field = ImGui::GetID("##name");
    const bool editing = naming_ == field;

    // While the field is being edited, its text lives in named_.
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%s", editing ? named_.c_str() : current.c_str());
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputText("##name", buffer, sizeof buffer);

    if (ImGui::IsItemActive()) {
        naming_ = field;
        named_ = buffer;
    } else if (editing) {
        naming_ = 0;
        // Escape puts the old text back; that is not a change.
        if (ImGui::IsItemDeactivatedAfterEdit() && named_ != current) return named_;
    }
    return std::nullopt;
}

void App::renameParameter(Param parameter, const std::string& name) {
    if (!canBeTypedInFormulas(name)) {
        notice_ = "'" + name + "' can't be a parameter name. Use letters, digits and underscores, starting with a letter; "
                               "not pi, min, max, abs, clamp, cm, mm or deg.";
        return;
    }
    if (parameterNamed(document(), name)) {
        notice_ = "There is already a parameter named '" + name + "'.";
        return;
    }
    later([parameter, name](Document& doc) { doc.rename(parameter, name); });
}

// ---- Left: parameters ---------------------------------------------------------------

void App::parametersPane() {
    ImGui::SeparatorText("Parameters");
    const float square = ImGui::GetFrameHeight();
    if (ImGui::BeginTable("parameters", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn("Formula", ImGuiTableColumnFlags_WidthStretch, 1.4f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.6f);
        ImGui::TableSetupColumn("##remove", ImGuiTableColumnFlags_WidthFixed, square);
        ImGui::TableHeadersRow();

        for (Param parameter : document().parameters()) {
            ImGui::PushID(static_cast<int>(parameter.value));
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            if (const std::optional<std::string> renamed = nameField(document().get(parameter).name))
                renameParameter(parameter, *renamed);

            ImGui::TableNextColumn();
            if (const std::optional<FieldEdit> edit = formulaField(document().get(parameter).value))
                apply(*edit, [&](Document& doc, const Expr& value) { doc.set(parameter, value); });

            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            if (problemWith(parameter)) ImGui::TextColored(color::error, "error");
            else ImGui::Text("%g", shapes()[parameter]);

            ImGui::TableNextColumn();
            if (ImGui::Button("x", ImVec2(square, square)))
                later([parameter](Document& doc) { doc.remove(parameter); });
            ImGui::SetItemTooltip("Remove this parameter");

            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (ImGui::Button("+ Add parameter"))
        later([](Document& doc) {
            const std::string name = firstFree("parameter", [&](const std::string& n) { return parameterNamed(doc, n); });
            doc.add(Parameter{.name = name, .value = 10});
        });

    for (Param parameter : document().parameters())
        if (const std::string* problem = problemWith(parameter))
            errorText(document().get(parameter).name + ": " + *problem);
    if (!notice_.empty()) errorText(notice_);

    ImGui::Spacing();
    ImGui::TextDisabled("Drag a number left or right to change it.");
    ImGui::TextDisabled("Click a field to type a number or a");
    ImGui::TextDisabled("formula such as waist / 4 + 1.");
    ImGui::TextDisabled("Lengths are in cm, angles in degrees.");
    ImGui::Spacing();
    ImGui::TextColored(color::driven, "This colour");
    ImGui::SameLine();
    ImGui::TextDisabled("marks a value driven by a formula.");
}

// ---- Right: details of the selected panel ----------------------------------------------------

void App::detailsPane() {
    ImGui::SeparatorText("Details");

    // Which panel. Panels are normally selected on the canvas; this list also
    // reaches one that has no outline to click.
    const std::string current = selected_ ? document().get(*selected_).name : std::string("no panel selected");
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##panel", current.c_str())) {
        for (PanelId panel : document().panels()) {
            ImGui::PushID(static_cast<int>(panel.value));
            if (ImGui::Selectable(document().get(panel).name.c_str(), selected_ && *selected_ == panel)) selected_ = panel;
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    if (!selected_) {
        ImGui::Spacing();
        ImGui::TextWrapped("Click a panel on the table, or draw one with the Rectangle tool.");
        return;
    }

    const PanelId panel = *selected_;
    ImGui::PushID(static_cast<int>(panel.value));
    if (ImGui::BeginTable("panel", 2)) {
        ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthStretch, 0.8f);
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch, 1.4f);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Name");
        ImGui::TableNextColumn();
        if (const std::optional<std::string> renamed = nameField(document().get(panel).name))
            later([panel, name = *renamed](Document& doc) { doc.rename(panel, name); });
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Angle on table");
        ImGui::SetItemTooltip("How far the panel is turned on the table, in degrees.\nThis only places it; it does not change the shape.\nTurn it with the knob above the panel.");
        ImGui::TableNextColumn();
        double angle = document().get(panel).angle;
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::InputDouble("##angle", &angle, 0.0, 0.0, "%.1f deg", ImGuiInputTextFlags_EnterReturnsTrue))
            later([panel, angle](Document& doc) { doc.turn(panel, std::remainder(angle, 360.0)); });
        ImGui::EndTable();
    }
    bool base = document().get(panel).construction;
    if (ImGui::Checkbox("Base only: not cut from cloth", &base))
        later([panel, base](Document& doc) { doc.setConstruction(panel, base); });

    arrangementOf(panel);
    seamsOf(panel);

    ImGui::SeparatorText("How it was made, in order");
    if (viewUntil_) {
        if (ImGui::Button("Show all steps")) viewUntil_.reset();
        ImGui::SameLine();
        ImGui::TextColored(color::driven, "showing an earlier step");
    } else {
        ImGui::TextDisabled("Click a step to see the panel as of then.");
        ImGui::TextDisabled("Untick a step to leave it out.");
    }
    operationsOf(panel);
    addOperationButton(panel);
    ImGui::PopID();
}

// Where the panel is in space: plain numbers in the same fields as the
// formulas (drag sideways, click to type), so a single click opens them.
// A typed formula is worked out once: placement stores plain numbers.
void App::arrangementOf(PanelId panel) {
    const sim::PanelMesh* mesh = session_.mesh()->find(panel);
    if (!show3D_ || !mesh) return;
    ImGui::SeparatorText("In space");
    const Arrangement now = sim::arrangementOf(document(), *mesh);
    const bool wasPlaced = now.placed;
    std::optional<Arrangement> changed;
    bool newStep = true;
    const auto row = [&](const char* label, const char* tip, double Arrangement::* value, double step, const Limits& limits) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::SetItemTooltip("%s", tip);
        ImGui::TableNextColumn();
        ImGui::PushID(label);
        if (const std::optional<FieldEdit> edit = formulaField(Expr(std::round(now.*value * 100.0) / 100.0), &limits, step)) {
            const std::optional<double> number = valueOf(edit->value, shapes());
            const bool dragged = drag_.has_value();
            if (!number) {
                notice_ = std::string("Not changed: ") + label + " needs a number, or a formula that works out now.";
            } else if (!dragged && !limits.allows(*number)) {
                notice_ = std::string("Not changed: ") + label + " " + limits.rule() + ".";
            } else {
                Arrangement next = now;
                next.*value = std::clamp(*number, limits.low, limits.high);  // a drag stops at the limit
                next.placed = true;
                newStep = !edit->continuesDrag;  // one undo step per drag or typed value
                if (drag_) drag_->changed = true;
                notice_.clear();
                changed = next;
            }
        }
        ImGui::PopID();
    };
    if (ImGui::BeginTable("space", 2)) {
        ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthStretch, 0.8f);
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch, 1.4f);
        const Limits place = Limits::between(-1000.0, 1000.0);
        row("Across (x)", "Where the panel's centre is, left to right, in cm. The body stands at 0.", &Arrangement::x, 0.5, place);
        row("Height (y)", "How high the panel's centre is above the floor, in cm.", &Arrangement::y, 0.5, place);
        row("Forward (z)", "Where the panel's centre is, back to front, in cm. The body faces forward from 0.", &Arrangement::z, 0.5, place);
        row("Turn", "Turns the panel about the vertical axis, in degrees. 180 puts it behind the body, facing forward.", &Arrangement::turn, 1.0, Limits::between(-360.0, 360.0));
        row("Tilt", "Leans the panel forward or back, in degrees.", &Arrangement::tilt, 1.0, Limits::between(-180.0, 180.0));
        row("Roll", "Turns the panel in its own plane, in degrees.", &Arrangement::roll, 1.0, Limits::between(-360.0, 360.0));
        row("Wrap", "Bends the panel around the body behind it, in degrees. 180 is half way round.", &Arrangement::wrap, 1.0, Limits::between(-350.0, 350.0));
        ImGui::EndTable();
    }
    if (changed) place(newStep, [&](Document& doc) { doc.arrange(panel, *changed); });
    if (wasPlaced) {
        if (ImGui::Button("Back to the table's place")) later([panel](Document& doc) { doc.arrange(panel, Arrangement{}); });
        ImGui::SetItemTooltip("Forget this arrangement: the panel stands in front of the body again, where the 2D table has it.");
    } else {
        ImGui::TextDisabled("Not arranged yet: it stands where the table has it.\nDrag it in the 3D view, or change a value here.");
    }
}

// The seams that touch a panel: what is sewn to what, both lengths, and
// which way round.
void App::seamsOf(PanelId panel) {
    std::vector<SeamId> mine;
    for (SeamId id : document().seams())
        if (document().get(id).a.panel == panel || document().get(id).b.panel == panel) mine.push_back(id);
    if (mine.empty()) return;
    ImGui::SeparatorText("Seams");
    for (SeamId id : mine) {
        const Seam& seam = document().get(id);
        ImGui::PushID(static_cast<int>(id.value));
        const auto sideName = [&](const SeamSide& side) {
            std::string name = nameOf(Reference(side.edge), document());
            if (!(side.panel == panel)) name += " on " + document().get(side.panel).name;
            return name;
        };
        const bool swap = !(seam.a.panel == panel);  // this panel's edge first
        const SeamSide& here = swap ? seam.b : seam.a;
        const SeamSide& there = swap ? seam.a : seam.b;
        ImGui::PushStyleColor(ImGuiCol_Text, color::seam);
        ImGui::TextWrapped("%s", sideName(here).c_str());
        ImGui::PopStyleColor();
        ImGui::TextWrapped("sewn to %s", sideName(there).c_str());
        const SeamResult* sewn = nullptr;
        for (const SeamResult& r : shapes().seams)
            if (r.seam == id) sewn = &r;
        if (sewn && !sewn->sewn()) {
            errorText("Broken: " + sewn->problem + ". It comes back if the edge does.");
        } else if (sewn) {
            const double hereLength = swap ? sewn->lengthB : sewn->lengthA, thereLength = swap ? sewn->lengthA : sewn->lengthB;
            if (std::abs(hereLength - thereLength) < 0.05) ImGui::TextDisabled("%.1f cm, both sides", hereLength);
            else ImGui::TextDisabled("%.1f cm to %.1f cm: the longer side is gathered", hereLength, thereLength);
        }
        bool reversed = seam.reversed;
        if (ImGui::Checkbox("Reversed", &reversed))
            later([id, reversed](Document& doc) { Seam changed = doc.get(id); changed.reversed = reversed; doc.set(id, changed); });
        ImGui::SetItemTooltip("Sews start to start instead of start to end, which twists the pieces.\nThe dotted links on the table show which ends meet.");
        ImGui::SameLine();
        if (ImGui::Button("Remove")) later([id](Document& doc) { if (doc.contains(id)) doc.remove(id); });
        ImGui::Spacing();
        ImGui::PopID();
    }
}

void App::operationsOf(PanelId panel) {
    const std::vector<OpId> operations = document().operations(panel);
    const float square = ImGui::GetFrameHeight();
    const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;

    for (std::size_t index = 0; index < operations.size(); ++index) {
        const OpId operation = operations[index];
        ImGui::PushID(static_cast<int>(operation.value));

        const std::string_view name = label(document().get(operation));
        const std::string* problem = problemWith(operation);

        // Heading: a tick box to leave the step out; its number and name, which
        // can be clicked to see the panel as of this step; then move up, move
        // down and remove on the right.
        const float rowStart = ImGui::GetCursorPosX();
        const float rowWidth = ImGui::GetContentRegionAvail().x;
        const float buttons = 3.0f * square + 2.0f * spacing;
        bool on = document().enabled(operation);
        if (ImGui::Checkbox("##on", &on)) later([operation, on](Document& doc) { doc.setEnabled(operation, on); });
        ImGui::SetItemTooltip(on ? "Untick to leave this step out." : "Tick to use this step again.");
        ImGui::SameLine();

        const bool viewed = viewUntil_ && *viewUntil_ == operation;
        const bool later_ = viewUntil_ && std::find(operations.begin(), operations.end(), *viewUntil_) < operations.begin() + static_cast<std::ptrdiff_t>(index);
        char heading[96];
        std::snprintf(heading, sizeof heading, "%d. %.*s%s", static_cast<int>(index) + 1, static_cast<int>(name.size()), name.data(),
                      on ? "" : "  (off)");
        if (problem) ImGui::PushStyleColor(ImGuiCol_Text, color::error);
        else if (!on || later_) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        const float nameWidth = std::max(rowStart + rowWidth - buttons - ImGui::GetStyle().ItemSpacing.x - ImGui::GetCursorPosX(), 20.0f);
        if (ImGui::Selectable(heading, viewed, ImGuiSelectableFlags_None, ImVec2(nameWidth, square))) {
            // Clicking the last step, or the step already shown, returns to the finished panel.
            if (viewed || index + 1 == operations.size()) viewUntil_.reset();
            else viewUntil_ = operation;
        }
        if (problem || !on || later_) ImGui::PopStyleColor();
        ImGui::SetItemTooltip(viewed ? "Click to show the finished panel again." : "Click to see the panel as of this step.");

        ImGui::SameLine(rowStart + rowWidth - buttons);
        ImGui::BeginDisabled(index == 0);
        if (ImGui::ArrowButton("up", ImGuiDir_Up)) later([operation, index](Document& doc) { doc.move(operation, index - 1); });
        ImGui::EndDisabled();
        ImGui::SameLine(0.0f, spacing);
        ImGui::BeginDisabled(index + 1 == operations.size());
        if (ImGui::ArrowButton("down", ImGuiDir_Down)) later([operation, index](Document& doc) { doc.move(operation, index + 1); });
        ImGui::EndDisabled();
        ImGui::SameLine(0.0f, spacing);
        if (ImGui::Button("x", ImVec2(square, square))) later([operation](Document& doc) { doc.remove(operation); });
        ImGui::SetItemTooltip("Remove this operation");

        if (const Offcut* offcut = std::get_if<Offcut>(&document().get(operation))) {
            const OpId cut = offcut->of.id;
            ImGui::TextDisabled("   %s", document().contains(cut)
                                             ? ("the part removed from " + document().get(document().panelOf(cut)).name).c_str()
                                             : "its cut was removed");
        }

        if (ImGui::BeginTable("fields", 2)) {
            ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthStretch, 0.8f);
            ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch, 1.4f);
            referenceRows(operation);
            choiceRows(operation);
            formulaRows(operation);
            ImGui::EndTable();
        }

        if (problem) errorText(*problem);
        ImGui::Spacing();
        ImGui::PopID();
    }
}

namespace {

void rowLabel(std::string_view text) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("   %.*s", static_cast<int>(text.size()), text.data());
    ImGui::TableNextColumn();
}

} // namespace

// What the operation works on: an edge, corner or line chosen by clicking on
// the canvas, or another panel chosen from a list.
void App::referenceRows(OpId operation) {
    const PanelId own = document().panelOf(operation);
    for (const ReferenceField& slot : references(document().get(operation))) {
        ImGui::PushID(1000 + static_cast<int>(slot.id.index));
        rowLabel(slot.label);
        const std::string current = nameOf(slot.value, document());
        const bool unset = current == "nothing chosen" || current.find("removed") != std::string::npos;

        if (std::holds_alternative<PanelId>(slot.value)) {
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::BeginCombo("##panel", current.c_str())) {
                for (PanelId other : document().panels()) {
                    if (other == own) continue;  // a panel can't be an instance of itself
                    if (ImGui::Selectable(document().get(other).name.c_str(), Reference(other) == slot.value))
                        later([operation, id = slot.id, other](Document& doc) { doc.set(operation, id, Reference(other)); });
                }
                ImGui::EndCombo();
            }
        } else {
            const bool choosing = picking_ && picking_->operation == operation && picking_->reference == slot.id;
            if (choosing) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            if (unset && !choosing) ImGui::PushStyleColor(ImGuiCol_Text, color::error);
            const std::string caption = choosing ? "click it on the canvas..." : unset ? current + ": click to choose" : current;
            if (ImGui::Button(caption.c_str(), ImVec2(-FLT_MIN, 0.0f))) {
                if (choosing) picking_.reset();
                else {
                    picking_ = Picking{operation, slot.id};
                    selected_ = own;
                }
            }
            if (unset && !choosing) ImGui::PopStyleColor();
            if (choosing) ImGui::PopStyleColor();
            ImGui::SetItemTooltip(choosing ? "Click again, or press Esc, to cancel." : "Click, then pick it on the canvas.");
        }
        ImGui::PopID();
    }
}

// Settings that are one of a few choices.
void App::choiceRows(OpId operation) {
    const OpData& data = document().get(operation);
    if (const Cut* cut = std::get_if<Cut>(&data)) {
        rowLabel("Keep");
        int side = cut->keep == Cut::Side::Left ? 0 : 1;
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::Combo("##keep", &side, "left of the line\0right of the line\0"))
            later([operation, side](Document& doc) {
                doc.set(OpRef<Cut>(operation), &Cut::keep, side == 0 ? Cut::Side::Left : Cut::Side::Right);
            });
    }
    if (const Mirror* mirror = std::get_if<Mirror>(&data)) {
        rowLabel("Applied last");
        bool last = mirror->appliedLast;
        if (ImGui::Checkbox("##last", &last))
            later([operation, last](Document& doc) { doc.set(OpRef<Mirror>(operation), &Mirror::appliedLast, last); });
        ImGui::SetItemTooltip("Ticked: the mirror waits for every other step, so later edits stay symmetric.\n"
                              "Unticked: it runs in its place, and later steps work on the whole panel.");
    }
    if (const Instance* instance = std::get_if<Instance>(&data)) {
        rowLabel("Mirrored");
        bool mirrored = instance->mirrored;
        if (ImGui::Checkbox("##mirrored", &mirrored))
            later([operation, mirrored](Document& doc) { doc.set(OpRef<Instance>(operation), &Instance::mirrored, mirrored); });
    }
}

void App::formulaRows(OpId operation) {
    // The fields are read into a list first, so changing one does not disturb the loop.
    for (const Field& field : fields(document().get(operation))) {
        ImGui::PushID(static_cast<int>(field.id.index));
        rowLabel(field.label);
        if (const std::optional<FieldEdit> edit = formulaField(field.value, &field.limits))
            apply(*edit, [&](Document& doc, const Expr& value) { doc.set(operation, field.id, value); });
        ImGui::PopID();
    }
}

void App::addOperationButton(PanelId panel) {
    const bool empty = document().operations(panel).empty();
    if (ImGui::Button("+ Add operation")) ImGui::OpenPopup("add operation");
    ImGui::SameLine();
    ImGui::TextDisabled(empty ? "start with a shape" : "or use the tools above the table");
    if (!ImGui::BeginPopup("add operation")) return;
    for (const Offered& choice : offered) {
        if (choice.starting != empty) continue;  // a shape first, then changes to it
        if (ImGui::MenuItem(choice.name, choice.hint)) {
            later([this, panel, &choice](Document& doc) {
                const OpId added = std::visit([&](auto operation) { return OpId(doc.add(panel, std::move(operation))); },
                                              choice.make(doc, panel));
                // If it needs an edge, corner or line that isn't chosen yet, ask for it right away.
                const std::vector<ReferenceField> slots = references(doc.get(added));
                if (!slots.empty() && !std::holds_alternative<PanelId>(slots[0].value) && nameOf(slots[0].value, doc) == "nothing chosen")
                    picking_ = Picking{added, slots[0].id};
            });
            if (empty) canvas_.fit();
        }
    }
    ImGui::EndPopup();
}

} // namespace clothcad::app
