#include "app/Canvas.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <initializer_list>
#include <string>
#include <vector>

#include <imgui.h>

#include "app/Locks.h"
#include "app/Style.h"
#include "core/Drawing.h"
#include "core/Fields.h"
#include "core/Text.h"

namespace clothcad::app {

namespace {

// A panel as the canvas draws it this frame.
struct Placed {
    PanelId panel;
    const Pattern* pattern;
    Vec2 offset;  // the panel's place on the table, added to its own coordinates
    Bounds box;   // where its outline ends up on the table
};

// `turned` keeps the panels' patterns as they are placed on the table, which
// may be turned from how they are built; the Placed entries point into it.
std::vector<Placed> placePanels(const Document& doc, const Result& shapes, std::deque<Pattern>& turnedPatterns) {
    std::vector<Placed> placed;
    for (PanelId panel : doc.panels()) {
        turnedPatterns.push_back(turned(shapes[panel], doc.get(panel).angle));
        const Pattern& pattern = turnedPatterns.back();
        const std::optional<Bounds> box = bounds(pattern);
        if (!box) continue;  // nothing to draw: it has no outline yet, or its starting operation failed
        const Vec2 offset{doc.get(panel).x, doc.get(panel).y};
        placed.push_back({panel, &pattern, offset,
                          Bounds{{box->min.x + offset.x, box->min.y + offset.y}, {box->max.x + offset.x, box->max.y + offset.y}}});
    }
    return placed;
}

// A line drawn as a repeating pattern of strokes and gaps, in units of
// pixel(): {6, 4} is dashed, {10, 4, 2, 4} is dash-dot.
void strokedLine(ImDrawList* draw, ImVec2 a, ImVec2 b, ImU32 colour, float thickness, std::initializer_list<float> pattern) {
    const float length = std::hypot(b.x - a.x, b.y - a.y);
    if (length < 1.0f || length > 1e6f) return;
    const ImVec2 along{(b.x - a.x) / length, (b.y - a.y) / length};
    float at = 0.0f;
    while (at < length) {
        bool stroke = true;
        for (float piece : pattern) {
            const float end = std::min(at + std::max(piece * pixel(), 1.0f), length);
            if (stroke && end > at)
                draw->AddLine({a.x + along.x * at, a.y + along.y * at}, {a.x + along.x * end, a.y + along.y * end}, colour, thickness);
            at = end;
            stroke = !stroke;
            if (at >= length) break;
        }
    }
}

// The panel this one is an instance of, if it is one.
const Instance* instanceBehind(const Document& doc, PanelId panel) {
    const std::vector<OpId> operations = doc.operations(panel);
    return operations.empty() ? nullptr : std::get_if<Instance>(&doc.get(operations.front()));
}

// The formula that sets an edge's length, as text; empty when the length was
// typed in as a number, or when a later operation (a cut) has changed it.
std::string formulaDriving(const Edge& edge, double length, const Document& doc, const Result& shapes) {
    if (!doc.contains(edge.name.op)) return {};
    const OpData& operation = doc.get(edge.name.op);
    for (const Dimension& dimension : dimensions(operation, edge.name.op)) {
        if (dimension.edge.role != edge.name.role) continue;  // mirrored copies share the role
        const Expr formula = fields(operation)[dimension.field.index].value;
        if (asNumber(formula)) return {};
        const std::optional<double> value = valueOf(formula, shapes);
        if (!value || std::abs(*value - length) > 1e-6) return {};
        return formatExpr(formula, doc);
    }
    return {};
}

// The grid's line spacing in centimetres for a zoom level: the smallest of
// 1 mm, 1 cm, 10 cm, 1 m ... whose lines are still comfortably apart on
// screen. Every tenth line is drawn stronger.
double gridSpacing(float pixelsPerCentimetre) {
    double spacing = 0.1;
    while (spacing * pixelsPerCentimetre < 9.0 * pixel() && spacing < 1e6) spacing *= 10.0;
    return spacing;
}

// "1 mm", "1 cm", "10 cm", "1 m" ...
std::string lengthName(double centimetres) {
    char text[32];
    if (centimetres < 1.0) std::snprintf(text, sizeof text, "%g mm", centimetres * 10.0);
    else if (centimetres < 100.0) std::snprintf(text, sizeof text, "%g cm", centimetres);
    else std::snprintf(text, sizeof text, "%g m", centimetres / 100.0);
    return text;
}

float distance(ImVec2 a, ImVec2 b) { return std::hypot(a.x - b.x, a.y - b.y); }

// How far a point on screen is from a line segment on screen.
float distanceToSegment(ImVec2 point, ImVec2 a, ImVec2 b) {
    const ImVec2 along{b.x - a.x, b.y - a.y};
    const float lengthSquared = along.x * along.x + along.y * along.y;
    float t = lengthSquared > 0.0f ? ((point.x - a.x) * along.x + (point.y - a.y) * along.y) / lengthSquared : 0.0f;
    t = std::clamp(t, 0.0f, 1.0f);
    return distance(point, {a.x + along.x * t, a.y + along.y * t});
}

float distanceToPolyline(ImVec2 point, const std::vector<ImVec2>& line) {
    float nearest = line.size() == 1 ? distance(point, line[0]) : 1e9f;
    for (std::size_t i = 0; i + 1 < line.size(); ++i) nearest = std::min(nearest, distanceToSegment(point, line[i], line[i + 1]));
    return nearest;
}

double roundTo(double value, double step) { return std::round(value / step) * step; }

struct Box {
    ImVec2 min, max;
    bool overlaps(const Box& other) const {
        return min.x < other.max.x && other.min.x < max.x && min.y < other.max.y && other.min.y < max.y;
    }
};

const char* hintFor(Tool tool) {
    switch (tool) {
    case Tool::Select: return "Select: click a panel. Drag it to move it; drag a corner or a curve handle to reshape it.";
    case Tool::Rectangle: return "Rectangle: drag on the table to draw a new panel.";
    case Tool::Circle: return "Circle: press where the centre goes and drag out to the radius.";
    case Tool::RingSector: return "Ring: click on the table to place a new ring panel, as for a circle skirt.";
    case Tool::Curve: return "Curve: click an edge to make it a free curve, then drag its handles.";
    case Tool::Line: return "Line: click a point on a panel's outline, then a second point on the same panel.";
    case Tool::Cut: return "Cut: click the part of a panel to remove, on one side of an internal line.";
    case Tool::Split: return "Split: click an internal line to split its panel into two panels.";
    case Tool::Mirror: return "Mirror: click a straight edge to complete the panel across it.";
    case Tool::Sew: return "Sew: click the first edge.";
    }
    return "";
}

} // namespace

void Canvas::cancel() {
    drag_.reset();
    rectangleFrom_.reset();
    lineFrom_.reset();
    sewFrom_.reset();
}

Canvas::ScreenPoint Canvas::toScreen(Vec2 tablePoint) const {
    return {originX_ + panX_ + static_cast<float>(tablePoint.x) * zoom_, originY_ + panY_ - static_cast<float>(tablePoint.y) * zoom_};
}

CanvasEvents Canvas::draw(const Document& doc, const Result& shapes, std::optional<PanelId> selected, Tool tool,
                          const PickRequest* pick, bool lookOnly) {
    if (lookOnly) tool = Tool::Select;
    if (tool != lastTool_) cancel();
    lastTool_ = tool;

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    originX_ = origin.x;
    originY_ = origin.y;
    ImVec2 size = ImGui::GetContentRegionAvail();
    size.x = std::max(size.x, 50.0f);
    size.y = std::max(size.y, 50.0f);

    // One invisible button over the whole area receives the mouse.
    ImGui::InvisibleButton("canvas", size,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                               ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();
    const ImGuiIO& io = ImGui::GetIO();
    const ImVec2 mouse = io.MousePos;
    const bool pressed = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    const bool released = ImGui::IsMouseReleased(ImGuiMouseButton_Left);

    std::deque<Pattern> turnedPatterns;
    const std::vector<Placed> placed = placePanels(doc, shapes, turnedPatterns);

    // ---- View: fit, pan, zoom ----
    if (fitRequested_ && !placed.empty()) {
        Bounds all = placed.front().box;
        for (const Placed& p : placed) {
            all.min.x = std::min(all.min.x, p.box.min.x);
            all.min.y = std::min(all.min.y, p.box.min.y);
            all.max.x = std::max(all.max.x, p.box.max.x);
            all.max.y = std::max(all.max.y, p.box.max.y);
        }
        const double width = std::max(all.max.x - all.min.x, 1.0), height = std::max(all.max.y - all.min.y, 1.0);
        zoom_ = std::clamp(static_cast<float>(std::min(size.x / (width * 1.25), size.y / (height * 1.5))), 0.2f, 200.0f);
        panX_ = size.x / 2.0f - static_cast<float>((all.min.x + all.max.x) / 2.0) * zoom_;
        panY_ = size.y / 2.0f + static_cast<float>((all.min.y + all.max.y) / 2.0) * zoom_;
        fitRequested_ = false;
    }
    if (held && (ImGui::IsMouseDragging(ImGuiMouseButton_Right) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle))) {
        panX_ += io.MouseDelta.x;
        panY_ += io.MouseDelta.y;
    }
    if (hovered && io.MouseWheel != 0.0f) {
        // Keep the point under the cursor where it is.
        const float newZoom = std::clamp(zoom_ * std::pow(1.15f, io.MouseWheel), 0.2f, 200.0f);
        const float applied = newZoom / zoom_;
        panX_ = (mouse.x - origin.x) - ((mouse.x - origin.x) - panX_) * applied;
        panY_ = (mouse.y - origin.y) - ((mouse.y - origin.y) - panY_) * applied;
        zoom_ = newZoom;
    }

    // The table's y axis points up; the screen's points down.
    const auto onScreen = [&](Vec2 point) {
        const ScreenPoint at = toScreen(point);
        return ImVec2{at.x, at.y};
    };
    const auto onTable = [&](ImVec2 at) { return Vec2{(at.x - origin.x - panX_) / zoom_, -(at.y - origin.y - panY_) / zoom_}; };
    const Vec2 mouseOnTable = onTable(mouse);
    const float px = pixel();

    // Holding Ctrl snaps what is being drawn or dragged to the grid's lines.
    const bool snapping = io.KeyCtrl;
    const double gridStep = gridSpacing(zoom_);
    const auto snapped = [&](Vec2 point) { return snapping ? Vec2{roundTo(point.x, gridStep), roundTo(point.y, gridStep)} : point; };
    const float reach = 10.0f * px;  // how close the cursor must be to grab something

    const auto within = [&](const Placed& p, Vec2 tablePoint) {
        return encloses(*p.pattern, Vec2{tablePoint.x - p.offset.x, tablePoint.y - p.offset.y});
    };
    const auto panelUnder = [&](Vec2 tablePoint) -> const Placed* {
        const Placed* found = nullptr;
        for (const Placed& p : placed)
            if (within(p, tablePoint) && (!found || (selected && p.panel == *selected))) found = &p;
        return found;
    };
    const auto edgeOnScreen = [&](const Placed& p, const Edge& edge) {
        std::vector<ImVec2> line;
        for (const Vec2& point : edgePoints(*p.pattern, edge)) line.push_back(onScreen({point.x + p.offset.x, point.y + p.offset.y}));
        return line;
    };
    const Placed* chosen = nullptr;
    for (const Placed& p : placed)
        if (selected && p.panel == *selected) chosen = &p;

    // The turning knob sits above the selected panel; the panel turns about
    // the centre of its box.
    const auto pivotOf = [&](const Placed& p) {
        const std::optional<Bounds> own = bounds(shapes[p.panel]);
        const Vec2 centre = own ? Vec2{(own->min.x + own->max.x) / 2.0, (own->min.y + own->max.y) / 2.0} : Vec2{};
        const Vec2 turnedCentre = turned(centre, doc.get(p.panel).angle);
        return Vec2{p.offset.x + turnedCentre.x, p.offset.y + turnedCentre.y};
    };
    const auto knobOf = [&](const Placed& p) {
        const ImVec2 top = onScreen(Vec2{(p.box.min.x + p.box.max.x) / 2.0, p.box.max.y});
        return ImVec2{top.x, top.y - 30.0f * px};
    };

    // ---- Colours ----
    const ImU32 background = ImGui::GetColorU32(ImGuiCol_FrameBg);
    const ImU32 text = ImGui::GetColorU32(ImGuiCol_Text);
    const ImU32 dim = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    const ImU32 outline = IM_COL32(200, 205, 215, 255);
    const ImU32 fill = IM_COL32(120, 150, 200, 40);
    const ImU32 instanceFill = IM_COL32(170, 130, 230, 45);
    const ImU32 chosenOutline = ImGui::GetColorU32(color::selected);
    const ImU32 chosenFill = IM_COL32(255, 190, 70, 45);
    const ImU32 internal = IM_COL32(120, 200, 140, 255);
    const ImU32 symmetry = ImGui::GetColorU32(color::symmetry);
    const ImU32 driven = ImGui::GetColorU32(color::driven);
    const ImU32 seamColour = ImGui::GetColorU32(color::seam);
    const ImU32 instanceText = ImGui::GetColorU32(color::instance);
    const ImU32 offered = IM_COL32(110, 220, 200, 255);
    const ImU32 handleColour = IM_COL32(255, 255, 255, 170);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 corner{origin.x + size.x, origin.y + size.y};
    draw->AddRectFilled(origin, corner, background);
    draw->PushClipRect(origin, corner, true);

    // ---- Grid ----
    // In real measurements: the lines are a fixed number of centimetres apart
    // on the table, whatever the zoom.
    if (gridVisible_) {
        const double fine = gridSpacing(zoom_);
        const double strong = fine * 10.0;
        const ImU32 fineLine = IM_COL32(255, 255, 255, 14);
        const ImU32 strongLine = IM_COL32(255, 255, 255, 38);
        const ImU32 zeroLine = IM_COL32(255, 255, 255, 80);
        const Vec2 low = onTable({origin.x, corner.y});   // bottom-left of what is visible
        const Vec2 high = onTable({corner.x, origin.y});  // top-right

        const auto lineKind = [&](long long index) { return index == 0 ? zeroLine : index % 10 == 0 ? strongLine : fineLine; };
        for (long long i = static_cast<long long>(std::floor(low.x / fine)); i * fine <= high.x; ++i) {
            const float x = onScreen(Vec2{i * fine, 0.0}).x;
            draw->AddLine({x, origin.y}, {x, corner.y}, lineKind(i), 1.0f * px);
        }
        for (long long i = static_cast<long long>(std::floor(low.y / fine)); i * fine <= high.y; ++i) {
            const float y = onScreen(Vec2{0.0, i * fine}).y;
            draw->AddLine({origin.x, y}, {corner.x, y}, lineKind(i), 1.0f * px);
        }
        const float textHeight = ImGui::GetFontSize();
        for (long long i = static_cast<long long>(std::floor(low.x / strong)); i * strong <= high.x; ++i) {
            char number[32];
            std::snprintf(number, sizeof number, "%g", i * strong);
            draw->AddText({onScreen(Vec2{i * strong, 0.0}).x + 4.0f * px, corner.y - textHeight - 4.0f * px}, dim, number);
        }
        for (long long i = static_cast<long long>(std::floor(low.y / strong)); i * strong <= high.y; ++i) {
            char number[32];
            std::snprintf(number, sizeof number, "%g", i * strong);
            const float y = onScreen(Vec2{0.0, i * strong}).y - textHeight - 2.0f * px;
            if (y < corner.y - 2.5f * textHeight) draw->AddText({origin.x + 4.0f * px, y}, dim, number);  // keep the corner clear
        }
        const std::string key = "Grid: " + lengthName(fine) + ", strong lines every " + lengthName(strong) + ". Numbers in cm.";
        const ImVec2 keySize = ImGui::CalcTextSize(key.c_str());
        draw->AddText({corner.x - keySize.x - 8.0f * px, corner.y - 2.2f * textHeight}, dim, key.c_str());
    }

    // The selected panel's length labels, for double-clicking.
    struct LengthLabel {
        ImVec2 min, max;
        EdgeRef edge;
    };
    std::vector<LengthLabel> lengthLabels;

    // ---- Panels ----
    for (const Placed& p : placed) {
        const bool isChosen = &p == chosen;
        const bool construction = doc.get(p.panel).construction;
        const Instance* instance = instanceBehind(doc, p.panel);
        const auto place = [&](Vec2 point) { return onScreen(Vec2{point.x + p.offset.x, point.y + p.offset.y}); };

        std::vector<ImVec2> polygon;
        for (const Vec2& point : outlinePoints(*p.pattern)) polygon.push_back(place(point));
        const int count = static_cast<int>(polygon.size());

        if (construction) {
            // A base that is never cut from cloth: dashed, not filled.
            for (int i = 0; i < count; ++i)
                strokedLine(draw, polygon[i], polygon[(i + 1) % count], isChosen ? chosenOutline : dim, 1.5f * px, {6.0f, 4.0f});
        } else {
            // Outlines run counter-clockwise; ImGui fills clockwise shapes.
            const std::vector<ImVec2> clockwise(polygon.rbegin(), polygon.rend());
            if (count >= 3) draw->AddConcavePolyFilled(clockwise.data(), count, isChosen ? chosenFill : instance ? instanceFill : fill);
            const ImU32 stroke = isChosen ? chosenOutline : outline;
            const float width = (isChosen ? 2.5f : 1.5f) * px;
            const bool hasSeam = std::any_of(p.pattern->edges.begin(), p.pattern->edges.end(), [](const Edge& e) { return e.seam; });
            if (!hasSeam) {
                draw->AddPolyline(polygon.data(), count, stroke, width, ImDrawFlags_Closed);
            } else {
                // A seam is where the cloth closes on itself: no line there.
                for (const Edge& edge : p.pattern->edges) {
                    if (edge.seam) continue;
                    std::vector<ImVec2> line;
                    for (const Vec2& point : edgePoints(*p.pattern, edge)) line.push_back(place(point));
                    draw->AddPolyline(line.data(), static_cast<int>(line.size()), stroke, width, ImDrawFlags_None);
                }
            }
        }

        // The cloth mesh: triangle sides, or why there are none.
        if (const sim::PanelMesh* mesh = mesh_ ? mesh_->find(p.panel) : nullptr) {
            const double angle = doc.get(p.panel).angle;
            std::vector<ImVec2> at;
            at.reserve(mesh->points.size());
            for (const Vec2& point : mesh->points) at.push_back(place(turned(point, angle)));
            const ImU32 wire = IM_COL32(200, 205, 215, 70);
            for (const std::array<uint32_t, 3>& t : mesh->triangles)
                for (int i = 0; i < 3; ++i)
                    if (t[i] < t[(i + 1) % 3])  // a side inside is shared by two triangles: draw it once. The outline is drawn anyway.
                        draw->AddLine(at[t[i]], at[t[(i + 1) % 3]], wire, 1.0f);
            if (!mesh->problem.empty()) {
                const ImVec2 middle = onScreen(Vec2{(p.box.min.x + p.box.max.x) / 2.0, (p.box.min.y + p.box.max.y) / 2.0});
                const std::string words = "No mesh: " + mesh->problem;
                const ImVec2 wordsSize = ImGui::CalcTextSize(words.c_str());
                draw->AddText({middle.x - wordsSize.x / 2.0f, middle.y}, ImGui::GetColorU32(color::error), words.c_str());
            }
        }

        for (const InternalLine& line : p.pattern->lines)
            strokedLine(draw, place(line.start), place(line.end), internal, 1.5f * px, {6.0f, 4.0f});

        // A mirror's axis: dash-dot, running a little past the outline.
        for (const SymmetryAxis& axis : p.pattern->axes) {
            const ImVec2 a = place(axis.start), b = place(axis.end);
            const float length = std::max(distance(a, b), 1.0f);
            const ImVec2 extra{(b.x - a.x) / length * 14.0f * px, (b.y - a.y) / length * 14.0f * px};
            strokedLine(draw, {a.x - extra.x, a.y - extra.y}, {b.x + extra.x, b.y + extra.y}, symmetry, 1.5f * px, {10.0f, 4.0f, 2.0f, 4.0f});
        }

        // A free curve's handles, on the selected panel. A handle that can be
        // dragged is a filled square; one that can't is hollow.
        if (isChosen)
            for (const Edge& edge : p.pattern->edges) {
                if (!edge.cubic) continue;
                const ImVec2 ends[2] = {place(p.pattern->vertices[edge.start].position), place(p.pattern->vertices[edge.end].position)};
                const ImVec2 controls[2] = {place(edge.cubic->control1), place(edge.cubic->control2)};
                for (int which = 0; which < 2; ++which) {
                    const bool free = handleLock(doc, p.panel, edge.name, which == 1).empty();
                    draw->AddLine(ends[which], controls[which], handleColour, 1.0f * px);
                    const ImVec2 lowCorner{controls[which].x - 4.0f * px, controls[which].y - 4.0f * px};
                    const ImVec2 highCorner{controls[which].x + 4.0f * px, controls[which].y + 4.0f * px};
                    if (free) draw->AddRectFilled(lowCorner, highCorner, handleColour);
                    else draw->AddRect(lowCorner, highCorner, handleColour, 0.0f, ImDrawFlags_None, 1.5f * px);
                }
            }

        // Corners. On the selected panel a corner that can be dragged is a
        // filled dot; one driven by a formula is a ring in the "driven" colour;
        // a selected one is larger, filled in the selection colour.
        for (const Vertex& vertex : p.pattern->vertices) {
            const ImVec2 at = place(vertex.position);
            const float radius = (isChosen ? 4.5f : 3.5f) * px;
            const bool isSelected = isChosen && std::find(selectedCorners_.begin(), selectedCorners_.end(), vertex.name) != selectedCorners_.end();
            if (isSelected) {
                draw->AddRectFilled({at.x - 6.5f * px, at.y - 6.5f * px}, {at.x + 6.5f * px, at.y + 6.5f * px}, chosenOutline);
                draw->AddRect({at.x - 6.5f * px, at.y - 6.5f * px}, {at.x + 6.5f * px, at.y + 6.5f * px}, background, 0.0f, ImDrawFlags_None, 1.5f * px);
            } else if (isChosen && !cornerLock(doc, p.panel, vertex.name).empty()) {
                draw->AddCircleFilled(at, radius, background);
                draw->AddCircle(at, radius, driven, 0, 2.0f * px);
            } else {
                draw->AddCircleFilled(at, radius, isChosen ? chosenOutline : outline);
                draw->AddCircle(at, radius, background, 0, 1.0f * px);
            }
        }

        // The knob that turns the panel.
        if (isChosen && tool == Tool::Select && !lookOnly) {
            const ImVec2 knob = knobOf(p);
            const ImVec2 top = onScreen(Vec2{(p.box.min.x + p.box.max.x) / 2.0, p.box.max.y});
            draw->AddLine(top, knob, chosenOutline, 1.0f * px);
            draw->AddCircleFilled(knob, 6.0f * px, background);
            draw->AddCircle(knob, 6.0f * px, chosenOutline, 0, 2.0f * px);
            draw->PathArcTo(knob, 3.0f * px, -1.2f, 3.4f);
            draw->PathStroke(chosenOutline, ImDrawFlags_None, 1.5f * px);
        }

        // Under the panel: its name, and what it follows if it is an instance.
        const std::string& name = doc.get(p.panel).name;
        const ImVec2 below = onScreen(Vec2{(p.box.min.x + p.box.max.x) / 2.0, p.box.min.y});
        const ImVec2 nameSize = ImGui::CalcTextSize(name.c_str());
        float line = below.y + nameSize.y * 1.7f;
        draw->AddText({below.x - nameSize.x / 2.0f, line}, isChosen ? chosenOutline : text, name.c_str());
        line += nameSize.y * 1.25f;
        std::string kind;
        if (instance)
            kind = std::string(instance->mirrored ? "mirrored instance of " : "instance of ") + nameOf(Reference(instance->source), doc);
        else if (construction) kind = "base, not cut from cloth";
        if (!kind.empty()) {
            const ImVec2 kindSize = ImGui::CalcTextSize(kind.c_str());
            draw->AddText({below.x - kindSize.x / 2.0f, line}, instance ? instanceText : dim, kind.c_str());
        }

        // Edge lengths on the selected panel. A length that comes from a
        // formula is shown in the "driven" colour together with the formula.
        if (isChosen) {
            std::vector<Box> taken;
            for (const Edge& edge : p.pattern->edges) {
                if (edge.seam) continue;
                const double length = p.pattern->length(edge.name);
                const std::string formula = formulaDriving(edge, length, doc, shapes);
                char number[32];
                std::snprintf(number, sizeof number, "%.1f", length);
                const std::string label = formula.empty() ? std::string(number) : std::string(number) + " = " + formula;

                // The label sits just outside the outline, beside the middle of its edge.
                const ImVec2 from = place(p.pattern->vertices[edge.start].position);
                const ImVec2 to = place(p.pattern->vertices[edge.end].position);
                const float chord = std::max(distance(from, to), 1.0f);
                const ImVec2 outward{-(to.y - from.y) / chord, (to.x - from.x) / chord};
                const ImVec2 labelSize = ImGui::CalcTextSize(label.c_str());
                const float away = labelSize.y * 0.9f + std::abs(outward.x) * labelSize.x * 0.5f;
                const ImVec2 middle = place(midpoint(*p.pattern, edge));
                const ImVec2 at{middle.x + outward.x * away - labelSize.x / 2.0f, middle.y + outward.y * away - labelSize.y / 2.0f};
                const Box box{{at.x - 4.0f * px, at.y - 2.0f * px}, {at.x + labelSize.x + 4.0f * px, at.y + labelSize.y + 2.0f * px}};
                // Two edges lying on top of each other would print one label over the other.
                if (std::any_of(taken.begin(), taken.end(), [&](const Box& other) { return box.overlaps(other); })) continue;
                taken.push_back(box);
                lengthLabels.push_back({box.min, box.max, edge.name});
                draw->AddRectFilled(box.min, box.max, background, 3.0f * px);
                draw->AddText(at, formula.empty() ? text : driven, label.c_str());
            }
        }
    }

    CanvasEvents events;
    std::string hint = placed.empty() && tool == Tool::Select ? "The table is empty. Pick the Rectangle tool above and drag here to draw a panel."
                                                              : hintFor(tool);

    // ---- Seams: both edges in the seam colour, their matched ends linked ----
    const auto placedPanel = [&](PanelId panel) -> const Placed* {
        for (const Placed& p : placed)
            if (p.panel == panel) return &p;
        return nullptr;
    };
    for (const SeamResult& sewn : shapes.seams) {
        if (!sewn.sewn() || !doc.contains(sewn.seam)) continue;
        const Seam& seam = doc.get(sewn.seam);
        const Placed* a = placedPanel(seam.a.panel);
        const Placed* b = placedPanel(seam.b.panel);
        if (!a || !b || !a->pattern->contains(seam.a.edge) || !b->pattern->contains(seam.b.edge)) continue;
        const std::vector<ImVec2> lineA = edgeOnScreen(*a, a->pattern->edge(seam.a.edge));
        const std::vector<ImVec2> lineB = edgeOnScreen(*b, b->pattern->edge(seam.b.edge));
        if (lineA.size() < 2 || lineB.size() < 2) continue;
        draw->AddPolyline(lineA.data(), static_cast<int>(lineA.size()), seamColour, 3.5f * px, ImDrawFlags_None);
        draw->AddPolyline(lineB.data(), static_cast<int>(lineB.size()), seamColour, 3.5f * px, ImDrawFlags_None);
        // Start of one edge to the end of the other, unless the seam is reversed. Crossed links show a twist.
        const ImVec2 startPartner = seam.reversed ? lineB.front() : lineB.back();
        const ImVec2 endPartner = seam.reversed ? lineB.back() : lineB.front();
        const ImU32 link = (seamColour & 0x00FFFFFF) | 0x90000000;
        strokedLine(draw, lineA.front(), startPartner, link, 1.0f * px, {3.0f, 4.0f});
        strokedLine(draw, lineA.back(), endPartner, link, 1.0f * px, {3.0f, 4.0f});
    }

    // =====================================================================
    // Picking for the details pane: offer the candidates on one panel.
    // =====================================================================
    if (pick) {
        const char* what = pick->kind == PickRequest::Kind::Edge ? "an edge" : pick->kind == PickRequest::Kind::Corner ? "a corner" : "a line";
        hint = std::string("Click ") + what + " on the highlighted panel. Esc cancels.";
        const auto owner = std::find_if(placed.begin(), placed.end(), [&](const Placed& p) { return p.panel == pick->panel; });
        const Pattern candidates = turned(pick->candidates, doc.contains(pick->panel) ? doc.get(pick->panel).angle : 0.0);
        const std::size_t offeredCount = pick->kind == PickRequest::Kind::Edge     ? candidates.edges.size()
                                         : pick->kind == PickRequest::Kind::Corner ? candidates.vertices.size()
                                                                                   : candidates.lines.size();
        if (owner == placed.end()) {
            hint = "This panel has no outline yet, so there is nothing to click. Esc cancels.";
        } else if (offeredCount == 0) {
            hint = pick->kind == PickRequest::Kind::Line
                       ? "This panel has no line to choose yet. Draw one with the Line tool first. Esc cancels."
                       : "There is nothing to choose on this panel at this point. Esc cancels.";
        } else {
            const auto place = [&](Vec2 point) { return onScreen(Vec2{point.x + owner->offset.x, point.y + owner->offset.y}); };
            const ImU32 faint = IM_COL32(110, 220, 200, 150);
            std::optional<Reference> nearest;
            float nearestDistance = reach;
            std::vector<ImVec2> nearestShape;
            const auto consider = [&](const Reference& candidate, const std::vector<ImVec2>& shape) {
                if (shape.size() == 1) draw->AddCircleFilled(shape[0], 6.0f * px, faint);
                else draw->AddPolyline(shape.data(), static_cast<int>(shape.size()), faint, 3.0f * px, ImDrawFlags_None);
                const float away = distanceToPolyline(mouse, shape);
                if (hovered && away < nearestDistance) {
                    nearestDistance = away;
                    nearest = candidate;
                    nearestShape = shape;
                }
            };
            if (pick->kind == PickRequest::Kind::Edge)
                for (const Edge& edge : candidates.edges) {
                    std::vector<ImVec2> shape;
                    for (const Vec2& point : edgePoints(candidates, edge)) shape.push_back(place(point));
                    consider(Reference(edge.name), shape);
                }
            if (pick->kind == PickRequest::Kind::Corner)
                for (const Vertex& vertex : candidates.vertices) consider(Reference(vertex.name), {place(vertex.position)});
            if (pick->kind == PickRequest::Kind::Line)
                for (const InternalLine& line : candidates.lines) consider(Reference(line.name), {place(line.start), place(line.end)});
            if (nearest) {
                if (nearestShape.size() == 1) draw->AddCircleFilled(nearestShape[0], 8.0f * px, offered);
                else draw->AddPolyline(nearestShape.data(), static_cast<int>(nearestShape.size()), offered, 6.0f * px, ImDrawFlags_None);
                ImGui::SetTooltip("%s", nameOf(*nearest, doc).c_str());
                if (pressed) events.picked = nearest;
            }
        }
    }

    // =====================================================================
    // Select: choose a panel; drag it, a corner, or a curve handle.
    // =====================================================================
    else if (tool == Tool::Select) {
        // What the cursor is over on the selected panel: a handle, else a corner.
        // A double-click on a length label asks to type that length.
        const LengthLabel* labelUnder = nullptr;
        for (const LengthLabel& label : lengthLabels)
            if (mouse.x >= label.min.x && mouse.x <= label.max.x && mouse.y >= label.min.y && mouse.y <= label.max.y) labelUnder = &label;
        if (labelUnder && hovered && !drag_ && !lookOnly) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImGui::SetTooltip("Double-click to type this length");
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                events.gesture = EditLength{chosen->panel, labelUnder->edge};
                draw->AddText({origin.x + 8.0f * px, origin.y + 6.0f * px}, dim, hint.c_str());
                draw->PopClipRect();
                return events;
            }
        }

        std::optional<Drag> grab;
        std::string locked;
        ImVec2 grabAt{};
        if (chosen && !drag_ && !lookOnly && !labelUnder) {
            const auto place = [&](Vec2 point) { return onScreen(Vec2{point.x + chosen->offset.x, point.y + chosen->offset.y}); };
            if (distance(mouse, knobOf(*chosen)) < reach) {
                grabAt = knobOf(*chosen);
                grab = Drag{Drag::Kind::Rotate, chosen->panel, {}, {}, false, mouseOnTable, pivotOf(*chosen), false, doc.get(chosen->panel).angle};
            }
            for (const Edge& edge : chosen->pattern->edges) {
                if (!edge.cubic) continue;
                const ImVec2 controls[2] = {place(edge.cubic->control1), place(edge.cubic->control2)};
                for (int which = 0; which < 2 && !grab && locked.empty(); ++which) {
                    if (distance(mouse, controls[which]) > reach) continue;
                    locked = handleLock(doc, chosen->panel, edge.name, which == 1);
                    grabAt = controls[which];
                    if (locked.empty()) grab = Drag{Drag::Kind::Handle, chosen->panel, {}, edge.name, which == 1, mouseOnTable, onTable(controls[which]), false};
                }
            }
            for (const Vertex& vertex : chosen->pattern->vertices) {
                if (grab || !locked.empty() || distance(mouse, place(vertex.position)) > reach) continue;
                locked = cornerLock(doc, chosen->panel, vertex.name);
                grabAt = place(vertex.position);
                if (locked.empty()) grab = Drag{Drag::Kind::Corner, chosen->panel, vertex.name, {}, false, mouseOnTable, onTable(grabAt), false};
            }
        }
        if (hovered && !drag_) {
            // Show what the cursor would take hold of.
            if (grab) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                draw->AddCircle(grabAt, 9.0f * px, offered, 0, 2.5f * px);
                if (grab->kind == Drag::Kind::Rotate) ImGui::SetTooltip("Drag to turn the panel. Ctrl: steps of 15 degrees.");
                else
                    ImGui::SetTooltip("%s", grab->kind == Drag::Kind::Corner ? (nameOf(Reference(grab->corner), doc) + "\nShift-click to select several. Shift-drag on the table to box-select.").c_str()
                                                                               : "curve handle");
            }
            if (!locked.empty()) ImGui::SetTooltip("%s", locked.c_str());
        }

        if (pressed && !labelUnder) {
            if (!grab && chosen && io.KeyShift && locked.empty() && !lookOnly) {
                drag_ = Drag{Drag::Kind::Box, chosen->panel, {}, {}, false, mouseOnTable, mouseOnTable, false};
            } else if (grab) {
                drag_ = grab;
            } else if (locked.empty()) {
                const Placed* under = panelUnder(mouseOnTable);
                events.gesture = SelectPanel{under ? std::optional<PanelId>(under->panel) : std::nullopt};
                if (under) drag_ = Drag{Drag::Kind::Panel, under->panel, {}, {}, false, mouseOnTable, under->offset, false};
            }
        }
        if (drag_ && drag_->kind == Drag::Kind::Rotate) {
            const Vec2 pivot = drag_->thingWasAt;
            const double now = std::atan2(mouseOnTable.y - pivot.y, mouseOnTable.x - pivot.x);
            const double was = std::atan2(drag_->pressedAt.y - pivot.y, drag_->pressedAt.x - pivot.x);
            double angle = drag_->angle0 + (now - was) * (180.0 / pi);
            angle = snapping ? roundTo(angle, 15.0) : roundTo(angle, 0.1);
            angle = std::remainder(angle, 360.0);
            const bool moving = ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3.0f * px);
            std::optional<DragPhase> phase;
            if (released && drag_->moved) phase = DragPhase::End;
            else if (!released && moving) phase = drag_->moved ? DragPhase::Continue : DragPhase::Begin;
            if (phase) {
                draw->AddLine(onScreen(pivot), mouse, offered, 1.5f * px);
                draw->AddCircleFilled(onScreen(pivot), 4.0f * px, offered);
                char label[48];
                std::snprintf(label, sizeof label, "%.1f degrees%s", angle, snapping ? "  (snapped)" : "");
                draw->AddText({mouse.x + 14.0f * px, mouse.y + 14.0f * px}, offered, label);
                events.gesture = RotatePanel{drag_->panel, angle, *phase};
                drag_->moved = true;
            }
            if (released || !ImGui::IsMouseDown(ImGuiMouseButton_Left)) drag_.reset();
        } else if (drag_ && drag_->kind == Drag::Kind::Box) {
            const ImVec2 a = onScreen(drag_->pressedAt);
            draw->AddRectFilled(a, mouse, IM_COL32(255, 190, 70, 30));
            draw->AddRect(a, mouse, chosenOutline, 0.0f, ImDrawFlags_None, 1.5f * px);
            if (released || !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                if (chosen && chosen->panel == drag_->panel) {
                    const double x0 = std::min(drag_->pressedAt.x, mouseOnTable.x), x1 = std::max(drag_->pressedAt.x, mouseOnTable.x);
                    const double y0 = std::min(drag_->pressedAt.y, mouseOnTable.y), y1 = std::max(drag_->pressedAt.y, mouseOnTable.y);
                    std::vector<CornerRef> inside;
                    for (const Vertex& vertex : chosen->pattern->vertices) {
                        const double x = vertex.position.x + chosen->offset.x, y = vertex.position.y + chosen->offset.y;
                        if (x >= x0 && x <= x1 && y >= y0 && y <= y1) inside.push_back(vertex.name);
                    }
                    events.gesture = SelectCorners{drag_->panel, std::move(inside), true};
                }
                drag_.reset();
            }
        } else if (drag_) {
            const double dx = mouseOnTable.x - drag_->pressedAt.x, dy = mouseOnTable.y - drag_->pressedAt.y;
            const bool moving = ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3.0f * px);
            std::optional<DragPhase> phase;
            if (released && drag_->moved) phase = DragPhase::End;
            else if (!released && moving) phase = drag_->moved ? DragPhase::Continue : DragPhase::Begin;
            if (phase) {
                // Where the thing ends up: on the grid when snapping; otherwise
                // whole millimetres, so the recorded numbers stay readable.
                const Vec2 target = snapped({drag_->thingWasAt.x + dx, drag_->thingWasAt.y + dy});
                const double stepX = roundTo(target.x - drag_->thingWasAt.x, snapping ? 1e-6 : 0.1);
                const double stepY = roundTo(target.y - drag_->thingWasAt.y, snapping ? 1e-6 : 0.1);
                // What is moving, and by how much.
                const ImVec2 thing = onScreen({drag_->thingWasAt.x + stepX, drag_->thingWasAt.y + stepY});
                if (drag_->kind != Drag::Kind::Panel) {
                    draw->AddCircle(onScreen(drag_->thingWasAt), 5.0f * px, dim, 0, 1.5f * px);  // where it was
                    draw->AddLine(onScreen(drag_->thingWasAt), thing, dim, 1.0f * px);
                    draw->AddCircle(thing, 9.0f * px, offered, 0, 2.5f * px);
                }
                char moved[64];
                std::snprintf(moved, sizeof moved, "%+.1f, %+.1f cm%s", stepX, stepY, snapping ? "  (snapped)" : "");
                draw->AddText({mouse.x + 14.0f * px, mouse.y + 14.0f * px}, offered, moved);
                switch (drag_->kind) {
                case Drag::Kind::Panel:
                    events.gesture = MovePanel{drag_->panel, drag_->thingWasAt.x + stepX, drag_->thingWasAt.y + stepY, *phase};
                    break;
                case Drag::Kind::Corner: events.gesture = MoveCorner{drag_->panel, drag_->corner, stepX, stepY, *phase}; break;
                case Drag::Kind::Handle: events.gesture = MoveHandle{drag_->panel, drag_->edge, drag_->endHandle, stepX, stepY, *phase}; break;
                case Drag::Kind::Rotate:
                case Drag::Kind::Box: break;
                }
                drag_->moved = true;
            }
            // A click on a corner, without dragging it: select it.
            if (released && !drag_->moved && drag_->kind == Drag::Kind::Corner)
                events.gesture = SelectCorners{drag_->panel, {drag_->corner}, io.KeyShift};
            if (released || !ImGui::IsMouseDown(ImGuiMouseButton_Left)) drag_.reset();
        }
    }

    // =====================================================================
    // Rectangle: drag out a new panel.
    // =====================================================================
    else if (tool == Tool::Rectangle) {
        if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (pressed) rectangleFrom_ = snapped(mouseOnTable);
        if (rectangleFrom_) {
            // Half centimetres, or the grid's lines when snapping.
            const double step = snapping ? gridStep : 0.5;
            const Vec2 to = snapped(mouseOnTable);
            const double x = roundTo(std::min(rectangleFrom_->x, to.x), step), y = roundTo(std::min(rectangleFrom_->y, to.y), step);
            const double width = roundTo(std::abs(to.x - rectangleFrom_->x), step), height = roundTo(std::abs(to.y - rectangleFrom_->y), step);
            const ImVec2 a = onScreen({x, y}), b = onScreen({x + width, y + height});
            draw->AddRect({a.x, b.y}, {b.x, a.y}, offered, 0.0f, ImDrawFlags_None, 2.0f * px);
            char label[48];
            std::snprintf(label, sizeof label, "%g x %g cm%s", width, height, snapping ? "  (snapped)" : "");
            draw->AddText({b.x + 8.0f * px, b.y}, offered, label);
            if (released || !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                if (width >= 1.0 && height >= 1.0) events.gesture = DrawRect{x, y, width, height};
                rectangleFrom_.reset();
            }
        }
    }

    // =====================================================================
    // Circle: press at the centre, drag out to the radius.
    // =====================================================================
    else if (tool == Tool::Circle) {
        if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (pressed) rectangleFrom_ = snapped({roundTo(mouseOnTable.x, 0.5), roundTo(mouseOnTable.y, 0.5)});
        if (rectangleFrom_) {
            const Vec2 to = snapped(mouseOnTable);
            const double radius = roundTo(std::hypot(to.x - rectangleFrom_->x, to.y - rectangleFrom_->y), snapping ? gridStep : 0.5);
            const ImVec2 centre = onScreen(*rectangleFrom_);
            draw->AddCircle(centre, static_cast<float>(radius) * zoom_, offered, 0, 2.0f * px);
            draw->AddCircleFilled(centre, 3.0f * px, offered);
            char label[48];
            std::snprintf(label, sizeof label, "radius %g cm%s", radius, snapping ? "  (snapped)" : "");
            draw->AddText({mouse.x + 14.0f * px, mouse.y + 14.0f * px}, offered, label);
            if (released || !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                if (radius >= 1.0) events.gesture = DrawCircle{rectangleFrom_->x, rectangleFrom_->y, radius};
                rectangleFrom_.reset();
            }
        }
    }

    // =====================================================================
    // Ring: click to place one.
    // =====================================================================
    else if (tool == Tool::RingSector) {
        if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        const Vec2 centre = snapped({roundTo(mouseOnTable.x, 0.5), roundTo(mouseOnTable.y, 0.5)});
        if (pressed) events.gesture = PlaceRing{centre.x, centre.y};
    }

    // =====================================================================
    // Curve and Mirror: click an edge.
    // =====================================================================
    else if (tool == Tool::Curve || tool == Tool::Mirror) {
        const Placed* nearestPanel = nullptr;
        const Edge* nearestEdge = nullptr;
        float nearestDistance = reach;
        std::vector<ImVec2> nearestShape;
        for (const Placed& p : placed)
            for (const Edge& edge : p.pattern->edges) {
                if (tool == Tool::Mirror && (edge.arc || edge.cubic)) continue;  // a mirror needs a straight edge
                const std::vector<ImVec2> shape = edgeOnScreen(p, edge);
                const float away = distanceToPolyline(mouse, shape);
                if (hovered && away < nearestDistance) {
                    nearestDistance = away;
                    nearestPanel = &p;
                    nearestEdge = &edge;
                    nearestShape = shape;
                }
            }
        if (nearestEdge) {
            draw->AddPolyline(nearestShape.data(), static_cast<int>(nearestShape.size()), offered, 6.0f * px, ImDrawFlags_None);
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImGui::SetTooltip("%s", nameOf(Reference(nearestEdge->name), doc).c_str());
            if (pressed) {
                if (tool == Tool::Curve) events.gesture = BendEdge{nearestPanel->panel, nearestEdge->name};
                else events.gesture = MirrorAcross{nearestPanel->panel, nearestEdge->name};
            }
        }
    }

    // =====================================================================
    // Sew: click one edge, then the edge to sew it to.
    // =====================================================================
    else if (tool == Tool::Sew) {
        hint = sewFrom_ ? "Sew: now click the edge to sew it to. Esc cancels." : "Sew: click the first edge.";
        const Placed* nearestPanel = nullptr;
        const Edge* nearestEdge = nullptr;
        float nearestDistance = reach;
        std::vector<ImVec2> nearestShape;
        for (const Placed& p : placed)
            for (const Edge& edge : p.pattern->edges) {
                if (edge.seam) continue;
                if (sewFrom_ && sewFrom_->panel == p.panel && sewFrom_->edge == edge.name) continue;
                const std::vector<ImVec2> shape = edgeOnScreen(p, edge);
                const float away = distanceToPolyline(mouse, shape);
                if (hovered && away < nearestDistance) {
                    nearestDistance = away;
                    nearestPanel = &p;
                    nearestEdge = &edge;
                    nearestShape = shape;
                }
            }
        if (sewFrom_) {
            const Placed* first = placedPanel(sewFrom_->panel);
            if (!first || !first->pattern->contains(sewFrom_->edge)) {
                sewFrom_.reset();
            } else {
                const std::vector<ImVec2> shape = edgeOnScreen(*first, first->pattern->edge(sewFrom_->edge));
                draw->AddPolyline(shape.data(), static_cast<int>(shape.size()), seamColour, 6.0f * px, ImDrawFlags_None);
                if (shape.size() >= 2) strokedLine(draw, shape[shape.size() / 2], mouse, seamColour, 1.5f * px, {6.0f, 4.0f});
            }
        }
        if (nearestEdge) {
            draw->AddPolyline(nearestShape.data(), static_cast<int>(nearestShape.size()), offered, 6.0f * px, ImDrawFlags_None);
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImGui::SetTooltip("%s\n%.1f cm", nameOf(Reference(nearestEdge->name), doc).c_str(), nearestPanel->pattern->length(nearestEdge->name));
            if (pressed) {
                const SeamSide side{nearestPanel->panel, nearestEdge->name};
                if (sewFrom_) {
                    events.gesture = SewEdges{*sewFrom_, side};
                    sewFrom_.reset();
                } else {
                    sewFrom_ = side;
                }
            }
        }
    }

    // =====================================================================
    // Line: two clicks on one panel's outline. Each snaps to the outline,
    // and to a corner when it is close to one.
    // =====================================================================
    else if (tool == Tool::Line) {
        struct Snap {
            const Placed* panel;
            OutlinePoint point;
            ImVec2 at;
        };
        std::optional<Snap> snap;
        float snapDistance = reach * 1.5f;
        for (const Placed& p : placed) {
            if (lineFrom_ && !(lineFrom_->panel == p.panel)) continue;  // the second end goes on the same panel
            std::optional<OutlinePoint> point = nearestOnOutline(*p.pattern, Vec2{mouseOnTable.x - p.offset.x, mouseOnTable.y - p.offset.y});
            if (!point) continue;
            // Close to an end of the edge: take the corner exactly.
            const Edge& edge = p.pattern->edge(point->edge);
            for (double end : {0.0, 1.0}) {
                const Vec2 cornerAt = p.pattern->vertices[end == 0.0 ? edge.start : edge.end].position;
                if (distance(mouse, onScreen({cornerAt.x + p.offset.x, cornerAt.y + p.offset.y})) < reach) {
                    point->along = end;
                    point->position = cornerAt;
                }
            }
            point->along = roundTo(point->along, 0.001);
            const ImVec2 at = onScreen({point->position.x + p.offset.x, point->position.y + p.offset.y});
            const float away = distance(mouse, at);
            if (hovered && away < snapDistance) {
                snapDistance = away;
                snap = Snap{&p, *point, at};
            }
        }
        if (lineFrom_) {
            const auto owner = std::find_if(placed.begin(), placed.end(), [&](const Placed& p) { return p.panel == lineFrom_->panel; });
            if (owner == placed.end()) {
                lineFrom_.reset();
            } else {
                const ImVec2 from = onScreen({lineFrom_->point.position.x + owner->offset.x, lineFrom_->point.position.y + owner->offset.y});
                strokedLine(draw, from, snap ? snap->at : mouse, offered, 2.0f * px, {6.0f, 4.0f});
                draw->AddCircleFilled(from, 5.0f * px, offered);
                hint = "Line: now click the second point on the same panel. Esc cancels.";
            }
        }
        if (snap) {
            draw->AddCircleFilled(snap->at, 5.0f * px, offered);
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            char where[96];
            std::snprintf(where, sizeof where, "%s, %.0f%% along", nameOf(Reference(snap->point.edge), doc).c_str(), snap->point.along * 100.0);
            ImGui::SetTooltip("%s", where);
            if (pressed) {
                if (!lineFrom_) {
                    lineFrom_ = LineStart{snap->panel->panel, snap->point};
                } else {
                    events.gesture = DrawLine{lineFrom_->panel, lineFrom_->point, snap->point};
                    lineFrom_.reset();
                }
            }
        }
    }

    // =====================================================================
    // Cut and Split: both work on a panel's internal lines.
    // =====================================================================
    else if (tool == Tool::Cut || tool == Tool::Split) {
        // The internal line nearest the cursor: for Split anywhere near it,
        // for Cut on the panel the cursor is inside.
        const Placed* linePanel = nullptr;
        const InternalLine* nearestLine = nullptr;
        float nearestDistance = tool == Tool::Split ? reach * 1.5f : 1e9f;
        const Placed* under = panelUnder(mouseOnTable);
        for (const Placed& p : placed) {
            if (tool == Tool::Cut && &p != under) continue;
            for (const InternalLine& line : p.pattern->lines) {
                const float away = distanceToSegment(mouse, onScreen({line.start.x + p.offset.x, line.start.y + p.offset.y}),
                                                     onScreen({line.end.x + p.offset.x, line.end.y + p.offset.y}));
                if (hovered && away < nearestDistance) {
                    nearestDistance = away;
                    linePanel = &p;
                    nearestLine = &line;
                }
            }
        }
        if (nearestLine) {
            const ImVec2 a = onScreen({nearestLine->start.x + linePanel->offset.x, nearestLine->start.y + linePanel->offset.y});
            const ImVec2 b = onScreen({nearestLine->end.x + linePanel->offset.x, nearestLine->end.y + linePanel->offset.y});
            draw->AddLine(a, b, offered, 4.0f * px);
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            if (tool == Tool::Split) {
                ImGui::SetTooltip("Split %s here into two panels", doc.get(linePanel->panel).name.c_str());
                if (pressed) events.gesture = SplitAt{linePanel->panel, nearestLine->name};
            } else {
                // Which side of the line the cursor is on, looking along the line; on screen y runs downward.
                const bool cursorIsLeft = (b.x - a.x) * (mouse.y - a.y) - (b.y - a.y) * (mouse.x - a.x) < 0.0f;
                ImGui::SetTooltip("Remove this part of %s", doc.get(linePanel->panel).name.c_str());
                if (pressed) events.gesture = CutAway{linePanel->panel, nearestLine->name, cursorIsLeft ? Cut::Side::Right : Cut::Side::Left};
            }
        } else if (hovered && under && tool == Tool::Cut) {
            ImGui::SetTooltip("This panel has no internal line yet. Draw one with the Line tool first.");
        }
    }

    if (lookOnly) hint = "Showing an earlier step of the selected panel. Choose \"Show all steps\" in the details to edit again.";
    else if (!pick && tool != Tool::Line && tool != Tool::Cut && tool != Tool::Split) hint += "  Hold Ctrl to snap to the grid.";
    draw->AddText({origin.x + 8.0f * px, origin.y + 6.0f * px}, pick || lookOnly ? driven : dim, hint.c_str());
    draw->PopClipRect();
    return events;
}

} // namespace clothcad::app
