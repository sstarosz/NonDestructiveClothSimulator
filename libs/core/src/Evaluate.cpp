#include "clothcad/core/Evaluate.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#include <stdexcept>
#include <utility>

#include "clothcad/core/Fields.h"
#include "internal/ExprNode.h"
#include "internal/Outline.h"

namespace clothcad {

namespace {

// A number, or the reason there isn't one.
struct Value {
    std::optional<double> number;
    std::string problem;

    static Value of(double v) { return {v, {}}; }
    static Value fail(std::string why) { return {std::nullopt, std::move(why)}; }
    explicit operator bool() const { return number.has_value(); }
};

class Evaluator {
public:
    explicit Evaluator(const Document& doc) : doc_(doc) {
        for (Param p : doc.parameters()) parameters_.push_back({p, State::Pending, 0.0});
        for (PanelId p : doc.panels()) panels_.push_back({p, State::Pending, {}});
    }

    Result run() {
        for (Param p : doc_.parameters()) valueOf(p);
        for (const ParameterState& s : parameters_)
            if (s.state == State::Done) result_.values.push_back({s.parameter, s.value});
        for (PanelId panel : doc_.panels()) patternOf(panel);
        for (PanelState& s : panels_) result_.panels.push_back({s.panel, std::move(s.pattern)});
        for (SeamId id : doc_.seams()) {
            const Seam& seam = doc_.get(id);
            SeamResult sewn{id, {}, 0.0, 0.0};
            const Pattern* a = shapeOf(seam.a.panel);
            const Pattern* b = shapeOf(seam.b.panel);
            const auto usable = [](const Pattern* p, const EdgeRef& e) { return p && p->contains(e) && !p->edge(e).seam; };
            if (!usable(a, seam.a.edge) || !usable(b, seam.b.edge)) sewn.problem = "an edge of this seam no longer exists";
            else if (seam.a == seam.b) sewn.problem = "an edge cannot be sewn to itself";
            else {
                sewn.lengthA = a->length(seam.a.edge);
                sewn.lengthB = b->length(seam.b.edge);
            }
            result_.seams.push_back(std::move(sewn));
        }
        return std::move(result_);
    }

private:
    const Pattern* shapeOf(PanelId panel) const {
        for (const PanelResult& p : result_.panels)
            if (p.panel == panel) return &p.pattern;
        return nullptr;
    }

    enum class State { Pending, Running, Done, Failed };
    struct ParameterState {
        Param parameter;
        State state;
        double value;
    };
    struct PanelState {
        PanelId panel;
        State state;
        Pattern pattern;
    };

    // Problems found while rebuilding part of another panel for an offcut are
    // that panel's to report, and it reports them when it is built itself.
    void error(OpId op, std::string message) {
        if (quiet_ == 0) result_.errors.push_back({op, std::move(message)});
    }

    // ---- Parameters ----

    ParameterState* find(Param p) {
        for (ParameterState& s : parameters_)
            if (s.parameter == p) return &s;
        return nullptr;
    }

    Value valueOf(Param p) {
        ParameterState* s = find(p);
        if (!s) return Value::fail("refers to a parameter that no longer exists");
        const std::string& name = doc_.get(p).name;
        switch (s->state) {
        case State::Done: return Value::of(s->value);
        case State::Failed: return Value::fail("depends on '" + name + "', which has an error");
        case State::Running: return Value::fail("circular reference through '" + name + "'");
        case State::Pending: break;
        }
        s->state = State::Running;
        const Value v = eval(doc_.get(p).value);
        if (v) {
            s->state = State::Done;
            s->value = *v.number;
            return v;
        }
        s->state = State::Failed;
        result_.errors.push_back({p, v.problem});
        return Value::fail("depends on '" + name + "', which has an error");
    }

    // ---- Expressions ----

    Value eval(const Expr& e) { return eval(ExprAccess::node(e)); }

    Value eval(const ExprNode& n) {
        using Kind = ExprNode::Kind;
        if (n.kind == Kind::Number) return Value::of(n.number);
        if (n.kind == Kind::Parameter) return valueOf(n.parameter);

        const Value a = eval(*n.a);
        if (!a) return a;
        if (n.kind == Kind::Negate) return Value::of(-*a.number);
        if (n.kind == Kind::Abs) return Value::of(std::abs(*a.number));

        const Value b = eval(*n.b);
        if (!b) return b;
        const double x = *a.number, y = *b.number;
        switch (n.kind) {
        case Kind::Add: return Value::of(x + y);
        case Kind::Subtract: return Value::of(x - y);
        case Kind::Multiply: return Value::of(x * y);
        case Kind::Divide: return y == 0.0 ? Value::fail("division by zero") : Value::of(x / y);
        case Kind::Min: return Value::of(std::min(x, y));
        case Kind::Max: return Value::of(std::max(x, y));
        default: return Value::fail("unsupported expression");
        }
    }

    // The values of an operation's formula fields, in fields() order, checked
    // against each field's limits. Nothing (with errors on the operation) if
    // any field has no value or breaks its limits.
    std::optional<std::vector<double>> fieldValues(const OpData& data, OpId op) {
        std::vector<double> values;
        bool usable = true;
        for (const Field& field : fields(data)) {
            const Value v = eval(field.value);
            if (!v) {
                error(op, std::string(field.label) + " " + v.problem);
                usable = false;
            } else if (!field.limits.allows(*v.number)) {
                char got[32];
                std::snprintf(got, sizeof got, "%g", *v.number);
                error(op, std::string(field.label) + " " + field.limits.rule() + "; it is " + got);
                usable = false;
            } else {
                values.push_back(*v.number);
            }
        }
        if (!usable) return std::nullopt;
        return values;
    }

    // ---- Panels ----

    // A panel's finished outline. Panels are built on demand so that an
    // instance can ask for its source; nullptr (with a reason) when the
    // panel is gone or the request loops back on itself.
    const Pattern* patternOf(PanelId panel, std::string* problem = nullptr) {
        PanelState* s = nullptr;
        for (PanelState& candidate : panels_)
            if (candidate.panel == panel) s = &candidate;
        if (!s) {
            if (problem) *problem = "the source panel no longer exists";
            return nullptr;
        }
        if (s->state == State::Running) {
            if (problem) *problem = "the panel is an instance of itself, directly or through other panels";
            return nullptr;
        }
        if (s->state == State::Pending) {
            s->state = State::Running;
            const int quiet = std::exchange(quiet_, 0);  // this is the panel's own build: report its problems
            Pattern built = build(panel);
            quiet_ = quiet;
            s->pattern = std::move(built);
            s->state = State::Done;
        }
        return &s->pattern;
    }

    // Runs a panel's operations top to bottom. A failed operation is reported
    // and skipped, so the panel keeps the result of the last good one.
    // Operations that are switched off are skipped. Mirrors marked
    // "applied last" wait until every other operation has run.
    //
    // With `stopBefore`, only the operations above that one are run, and no
    // waiting mirror: the result is what that operation gets to work on.
    Pattern build(PanelId panel, std::optional<OpId> stopBefore = std::nullopt) {
        Pattern pattern;
        bool first = true;
        std::vector<OpId> waiting;
        for (OpId op : doc_.operations(panel)) {
            if (stopBefore && op == *stopBefore) return pattern;
            if (!doc_.enabled(op)) continue;
            const OpData& data = doc_.get(op);
            if (const Mirror* mirror = std::get_if<Mirror>(&data); mirror && mirror->appliedLast) {
                waiting.push_back(op);
                continue;
            }
            if (const std::optional<std::vector<double>> values = fieldValues(data, op))
                std::visit([&](const auto& typed) { apply(typed, *values, op, first, pattern); }, data);
            first = false;
        }
        for (OpId op : waiting) {
            apply(std::get<Mirror>(doc_.get(op)), {}, op, first, pattern);
            first = false;
        }
        return pattern;
    }

    // Starting operations create the outline, so they must come first.
    bool starts(bool first, const char* what, OpId op) {
        if (!first) error(op, std::string(what) + " must be the first operation of its panel");
        return first;
    }

    // Modifying operations need an outline. If the starting operation failed
    // there is none, and that failure is already reported.
    bool modifies(bool first, const char* what, OpId op, const Pattern& pattern) {
        if (first) error(op, std::string(what) + " needs an outline: add a starting operation before it");
        return !first && !pattern.edges.empty();
    }

    // ---- Starting operations ----

    // Each apply() gets its operation's field values in fields() order,
    // already checked against their limits.

    void apply(const Rect&, const std::vector<double>& value, OpId op, bool first, Pattern& pattern) {
        if (!starts(first, "a rectangle", op)) return;
        const double* w = &value[0];
        const double* h = &value[1];

        const OpRef<Rect> names(op);
        pattern.vertices = {
            {names.bottomLeft(), {0.0, 0.0}},
            {names.bottomRight(), {*w, 0.0}},
            {names.topRight(), {*w, *h}},
            {names.topLeft(), {0.0, *h}},
        };
        pattern.edges = {
            {names.bottom(), 0, 1, std::nullopt},
            {names.right(), 1, 2, std::nullopt},
            {names.top(), 2, 3, std::nullopt},
            {names.left(), 3, 0, std::nullopt},
        };
    }

    void apply(const Circle&, const std::vector<double>& value, OpId op, bool first, Pattern& pattern) {
        if (!starts(first, "a circle", op)) return;
        const double radius = value[0], sweep = value[1];
        const OpRef<Circle> names(op);
        const Vec2 centre{0.0, 0.0};
        if (sweep > 360.0 - 1e-9) {
            // A whole circle: one edge that starts and ends at its lowest point.
            pattern.vertices = {{names.start(), {0.0, -radius}}};
            pattern.edges = {{names.arc(), 0, 0, Arc{centre, radius, 360.0}}};
            return;
        }
        // A slice, hanging downward: centred on the negative y axis.
        const double radians = sweep * (pi / 180.0);
        const double startAngle = -pi / 2.0 - radians / 2.0, endAngle = -pi / 2.0 + radians / 2.0;
        pattern.vertices = {
            {names.start(), {radius * std::cos(startAngle), radius * std::sin(startAngle)}},
            {names.end(), {radius * std::cos(endAngle), radius * std::sin(endAngle)}},
            {names.centre(), centre},
        };
        pattern.edges = {
            {names.arc(), 0, 1, Arc{centre, radius, sweep}},
            {names.endSide(), 1, 2, std::nullopt},
            {names.startSide(), 2, 0, std::nullopt},
        };
    }

    void apply(const RingSector&, const std::vector<double>& value, OpId op, bool first, Pattern& pattern) {
        if (!starts(first, "a ring sector", op)) return;
        const double* innerLength = &value[0];
        const double* sweep = &value[1];
        const double* depth = &value[2];

        const double radians = *sweep * (pi / 180.0);
        const double inner = *innerLength / radians;  // arc length = radius * angle
        const double outer = inner + *depth;

        // Hangs downward: centred on the negative y axis.
        const double startAngle = -pi / 2.0 - radians / 2.0;
        const double endAngle = -pi / 2.0 + radians / 2.0;
        const auto at = [](double radius, double angle) {
            return Vec2{radius * std::cos(angle), radius * std::sin(angle)};
        };

        const OpRef<RingSector> names(op);
        const Vec2 centre{0.0, 0.0};
        pattern.vertices = {
            {names.outerStart(), at(outer, startAngle)},
            {names.outerEnd(), at(outer, endAngle)},
            {names.innerEnd(), at(inner, endAngle)},
            {names.innerStart(), at(inner, startAngle)},
        };
        pattern.edges = {
            {names.outer(), 0, 1, Arc{centre, outer, *sweep}},
            {names.endSide(), 1, 2, std::nullopt},
            {names.inner(), 2, 3, Arc{centre, inner, -*sweep}},
            {names.startSide(), 3, 0, std::nullopt},
        };
        // A whole turn closes on itself: the two sides are one seam, not edges.
        if (*sweep > 360.0 - 1e-9) pattern.edges[1].seam = pattern.edges[3].seam = true;
    }

    void apply(const Instance& instance, const std::vector<double>&, OpId op, bool first, Pattern& pattern) {
        if (!starts(first, "an instance", op)) return;
        std::string problem;
        const Pattern* source = patternOf(instance.source, &problem);
        if (!source) {
            error(op, problem);
            return;
        }
        pattern = *source;
        if (instance.mirrored) outline::flip(pattern);
    }

    void apply(const Offcut& offcut, const std::vector<double>&, OpId op, bool first, Pattern& pattern) {
        if (!starts(first, "an offcut", op)) return;
        const OpId cutId = offcut.of.id;
        const Cut* cut = doc_.contains(cutId) ? std::get_if<Cut>(&doc_.get(cutId)) : nullptr;
        if (!cut) {
            error(op, "the cut this piece comes from no longer exists");
            return;
        }
        if (depth_ >= 16) {
            error(op, "panels are cut from each other in a loop");
            return;
        }
        // Rebuild the other panel up to its cut, then cut the other way.
        ++depth_;
        ++quiet_;
        Pattern whole = build(doc_.panelOf(cutId), cutId);
        --quiet_;
        --depth_;
        if (!whole.contains(cut->along)) {
            error(op, "the cut this piece comes from has no line to follow");
            return;
        }
        const InternalLine line = whole.line(cut->along);
        const outline::Problem problem = outline::cut(whole, line.start, line.end, cut->keep != Cut::Side::Left,
                                                      offcut.of.edge(), offcut.of.start(), offcut.of.end());
        if (problem) error(op, *problem);
        else pattern = std::move(whole);
    }

    // ---- Modifying operations ----

    void apply(const Curve& curve, const std::vector<double>& value, OpId op, bool first, Pattern& pattern) {
        if (!modifies(first, "a curve", op, pattern)) return;
        const auto edge = std::find_if(pattern.edges.begin(), pattern.edges.end(),
                                       [&](const Edge& e) { return e.name == curve.edge; });
        if (edge == pattern.edges.end()) {
            error(op, "the edge to curve does not exist; an earlier operation may have removed it");
            return;
        }
        const double* bulge = &value[0];
        edge->cubic.reset();
        edge->arc = outline::arcFromBulge(pattern.vertices[edge->start].position,
                                          pattern.vertices[edge->end].position, *bulge);
    }

    void apply(const Bezier& bezier, const std::vector<double>& value, OpId op, bool first, Pattern& pattern) {
        if (!modifies(first, "a free curve", op, pattern)) return;
        const auto edge = std::find_if(pattern.edges.begin(), pattern.edges.end(),
                                       [&](const Edge& e) { return e.name == bezier.edge; });
        if (edge == pattern.edges.end()) {
            error(op, "the edge to curve does not exist; an earlier operation may have removed it");
            return;
        }
        const double* startX = &value[0];
        const double* startY = &value[1];
        const double* endX = &value[2];
        const double* endY = &value[3];
        const Vec2 start = pattern.vertices[edge->start].position;
        const Vec2 end = pattern.vertices[edge->end].position;
        edge->arc.reset();
        edge->cubic = CubicCurve{{start.x + *startX, start.y + *startY}, {end.x + *endX, end.y + *endY}};
    }

    void apply(const Line& line, const std::vector<double>& value, OpId op, bool first, Pattern& pattern) {
        if (!modifies(first, "a line", op, pattern)) return;
        if (!pattern.contains(line.from)) {
            error(op, "the corner the line starts from does not exist; an earlier operation may have removed it");
            return;
        }
        const double* angle = &value[0];
        const Vec2 start = pattern.position(line.from);
        const std::optional<Vec2> end = outline::firstHit(pattern, start, *angle);
        if (!end) {
            error(op, "the line points away from the panel and never crosses it");
            return;
        }
        pattern.lines.push_back({LineRef{op}, start, *end});
    }

    void apply(const LineBetween& line, const std::vector<double>& value, OpId op, bool first, Pattern& pattern) {
        if (!modifies(first, "a line", op, pattern)) return;
        if (!pattern.contains(line.from.edge) || !pattern.contains(line.to.edge)) {
            error(op, "an edge the line is attached to does not exist; an earlier operation may have removed it");
            return;
        }
        const Vec2 start = outline::pointAt(pattern, pattern.edge(line.from.edge), value[0]);
        const Vec2 end = outline::pointAt(pattern, pattern.edge(line.to.edge), value[1]);
        if (std::hypot(end.x - start.x, end.y - start.y) < 1e-6) {
            error(op, "the line starts and ends at the same point");
            return;
        }
        pattern.lines.push_back({LineRef{op}, start, end});
    }

    void apply(const Cut& cut, const std::vector<double>&, OpId op, bool first, Pattern& pattern) {
        if (!modifies(first, "a cut", op, pattern)) return;
        if (!pattern.contains(cut.along)) {
            error(op, "the line to cut along does not exist in this panel");
            return;
        }
        const InternalLine line = pattern.line(cut.along);
        const OpRef<Cut> names(op);
        Pattern changed = pattern;
        const outline::Problem problem = outline::cut(changed, line.start, line.end, cut.keep == Cut::Side::Left,
                                                      names.edge(), names.start(), names.end());
        if (problem) error(op, *problem);
        else pattern = std::move(changed);
    }

    void apply(const Mirror& mirror, const std::vector<double>&, OpId op, bool first, Pattern& pattern) {
        if (!modifies(first, "a mirror", op, pattern)) return;
        Pattern changed = pattern;
        const outline::Problem problem = outline::mirror(changed, mirror.across, op);
        if (problem) error(op, *problem);
        else pattern = std::move(changed);
    }

    void apply(const AdjustPoint& adjust, const std::vector<double>& value, OpId op, bool first, Pattern& pattern) {
        if (!modifies(first, "a point adjustment", op, pattern)) return;
        const auto vertex = std::find_if(pattern.vertices.begin(), pattern.vertices.end(),
                                         [&](const Vertex& v) { return v.name == adjust.corner; });
        if (vertex == pattern.vertices.end()) {
            error(op, "the corner to adjust does not exist; an earlier operation may have removed it");
            return;
        }
        const uint32_t index = static_cast<uint32_t>(vertex - pattern.vertices.begin());
        for (const Edge& e : pattern.edges)
            if (e.arc && (e.start == index || e.end == index) && (e.start == e.end || std::abs(e.arc->sweep) > 360.0 - 1e-6)) {
                error(op, "the corner belongs to a whole turn of a circle, which has no ends to move; change its radius instead");
                return;
            }
        const double* dx = &value[0];
        const double* dy = &value[1];
        vertex->position.x += *dx;
        vertex->position.y += *dy;
        // A free curve's handle at this corner travels with it.
        for (Edge& e : pattern.edges) {
            if (!e.cubic) continue;
            if (e.start == index) e.cubic->control1 = {e.cubic->control1.x + *dx, e.cubic->control1.y + *dy};
            if (e.end == index) e.cubic->control2 = {e.cubic->control2.x + *dx, e.cubic->control2.y + *dy};
        }
        // An arc keeps turning through the same angle, between the new ends.
        for (Edge& e : pattern.edges) {
            if (!e.arc || (e.start != index && e.end != index)) continue;
            const std::optional<Arc> arc = outline::arcFromSweep(pattern.vertices[e.start].position, pattern.vertices[e.end].position, e.arc->sweep);
            if (!arc) {
                error(op, "the corner cannot move there: the curved edge would have no length");
                return;
            }
            e.arc = arc;
        }
    }

    const Document& doc_;
    std::vector<ParameterState> parameters_;
    std::vector<PanelState> panels_;
    Result result_;
    int quiet_ = 0;  // above zero: operation problems are not recorded
    int depth_ = 0;  // how many offcuts deep the current build is
};

} // namespace

const SeamResult& Result::operator[](SeamId seam) const {
    for (const SeamResult& s : seams)
        if (s.seam == seam) return s;
    throw std::out_of_range("clothcad::Result: unknown seam");
}

bool Result::failed(Param parameter) const {
    return std::any_of(errors.begin(), errors.end(), [&](const Error& e) {
        const Param* p = std::get_if<Param>(&e.where);
        return p && *p == parameter;
    });
}

bool Result::failed(OpId operation) const {
    return std::any_of(errors.begin(), errors.end(), [&](const Error& e) {
        const OpId* o = std::get_if<OpId>(&e.where);
        return o && *o == operation;
    });
}

double Result::operator[](Param parameter) const {
    for (const ParameterValue& v : values)
        if (v.parameter == parameter) return v.value;
    throw std::out_of_range("clothcad::Result: parameter has no value");
}

const Pattern& Result::operator[](PanelId panel) const {
    for (const PanelResult& p : panels)
        if (p.panel == panel) return p.pattern;
    throw std::out_of_range("clothcad::Result: unknown panel");
}

namespace {

// The same arithmetic as Evaluator::eval, reading parameters from a finished
// Result. Keep the two in step when adding a kind of expression.
std::optional<double> compute(const ExprNode& n, const Result& result) {
    using Kind = ExprNode::Kind;
    if (n.kind == Kind::Number) return n.number;
    if (n.kind == Kind::Parameter) {
        for (const ParameterValue& v : result.values)
            if (v.parameter == n.parameter) return v.value;
        return std::nullopt;
    }
    const std::optional<double> a = compute(*n.a, result);
    if (!a) return std::nullopt;
    if (n.kind == Kind::Negate) return -*a;
    if (n.kind == Kind::Abs) return std::abs(*a);
    const std::optional<double> b = compute(*n.b, result);
    if (!b) return std::nullopt;
    switch (n.kind) {
    case Kind::Add: return *a + *b;
    case Kind::Subtract: return *a - *b;
    case Kind::Multiply: return *a * *b;
    case Kind::Divide: return *b == 0.0 ? std::nullopt : std::optional<double>(*a / *b);
    case Kind::Min: return std::min(*a, *b);
    case Kind::Max: return std::max(*a, *b);
    default: return std::nullopt;
    }
}

} // namespace

std::optional<double> valueOf(const Expr& expr, const Result& result) {
    return compute(ExprAccess::node(expr), result);
}

std::size_t bake(Document& doc, PanelId panel) {
    const Result result = evaluate(doc);
    std::size_t replaced = 0;
    for (OpId op : doc.operations(panel))
        for (const Field& field : fields(doc.get(op))) {
            if (asNumber(field.value)) continue;
            if (const std::optional<double> value = valueOf(field.value, result)) {
                doc.set(op, field.id, Expr(*value));
                ++replaced;
            }
        }
    return replaced;
}

Pattern outlineBefore(const Document& doc, OpId operation) {
    if (!doc.contains(operation)) return {};
    // Evaluate a copy that stops short of the operation. A mirror that is
    // applied last has not happened yet from the operation's point of view.
    Document shortened = doc;
    const PanelId panel = doc.panelOf(operation);
    bool reached = false;
    for (OpId op : doc.operations(panel)) {
        reached = reached || op == operation;
        const Mirror* mirror = std::get_if<Mirror>(&doc.get(op));
        if (reached || (mirror && mirror->appliedLast)) shortened.remove(op);
    }
    return evaluate(shortened)[panel];
}

Result evaluateUpTo(const Document& doc, OpId last) {
    if (!doc.contains(last)) return evaluate(doc);
    Document shortened = doc;
    bool past = false;
    for (OpId op : doc.operations(doc.panelOf(last))) {
        const Mirror* mirror = std::get_if<Mirror>(&doc.get(op));
        if (past && !(mirror && mirror->appliedLast)) shortened.setEnabled(op, false);
        past = past || op == last;
    }
    return evaluate(shortened);
}

Result evaluate(const Document& doc) {
    try {
        return Evaluator(doc).run();
    } catch (const std::exception& e) {
        Result failed;
        failed.errors.push_back({Param{}, std::string("internal error: ") + e.what()});
        return failed;
    }
}

} // namespace clothcad
