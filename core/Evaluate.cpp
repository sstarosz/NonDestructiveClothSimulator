#include "core/Evaluate.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>

#include "core/internal/ExprNode.h"

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
        for (Param p : doc.parameters()) states_.push_back({p, State::Pending, 0.0});
    }

    Result run() {
        for (Param p : doc_.parameters()) valueOf(p);
        for (const ParameterState& s : states_)
            if (s.state == State::Done) result_.values.push_back({s.parameter, s.value});
        for (PanelId panel : doc_.panels()) result_.panels.push_back({panel, build(panel)});
        return std::move(result_);
    }

private:
    enum class State { Pending, Running, Done, Failed };
    struct ParameterState {
        Param parameter;
        State state;
        double value;
    };

    // ---- Parameters ----

    ParameterState* find(Param p) {
        for (ParameterState& s : states_)
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
        s = find(p);
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

    // A field's value, or nothing (with an error on the operation).
    std::optional<double> number(const Expr& e, const char* field, OpId op) {
        const Value v = eval(e);
        if (!v) result_.errors.push_back({op, std::string(field) + " " + v.problem});
        return v.number;
    }

    // A field that must be greater than zero.
    std::optional<double> positive(const Expr& e, const char* field, OpId op) {
        const std::optional<double> v = number(e, field, op);
        if (v && !(*v > 0.0)) {
            result_.errors.push_back({op, std::string(field) + " must be greater than zero"});
            return std::nullopt;
        }
        return v;
    }

    // Starting operations create the outline, so they must come first.
    bool isFirst(bool first, const char* what, OpId op) {
        if (!first) result_.errors.push_back({op, std::string(what) + " must be the first operation of its panel"});
        return first;
    }

    // ---- Panels ----

    // Runs a panel's operations top to bottom. A failed operation is reported
    // and skipped, so the panel keeps the result of the last good one.
    Pattern build(PanelId panel) {
        Pattern pattern;
        bool first = true;
        for (OpId op : doc_.operations(panel)) {
            std::visit([&](const auto& data) { apply(data, op, first, pattern); }, doc_.get(op));
            first = false;
        }
        return pattern;
    }

    void apply(const Rect& rect, OpId op, bool first, Pattern& pattern) {
        if (!isFirst(first, "a rectangle", op)) return;
        const std::optional<double> w = positive(rect.width, "width", op);
        const std::optional<double> h = positive(rect.height, "height", op);
        if (!w || !h) return;

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

    void apply(const RingSector& ring, OpId op, bool first, Pattern& pattern) {
        if (!isFirst(first, "a ring sector", op)) return;
        const std::optional<double> innerLength = positive(ring.innerLength, "inner length", op);
        std::optional<double> sweep = positive(ring.sweep, "sweep", op);
        const std::optional<double> depth = positive(ring.depth, "depth", op);
        if (sweep && *sweep > 360.0 + 1e-9) {
            result_.errors.push_back({op, "sweep must be at most 360 degrees"});
            sweep.reset();
        }
        if (!innerLength || !sweep || !depth) return;

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
    }

    const Document& doc_;
    std::vector<ParameterState> states_;
    Result result_;
};

} // namespace

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
