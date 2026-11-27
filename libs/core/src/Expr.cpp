#include "twintailor/core/Expr.h"

#include "internal/ExprNode.h"

namespace twintailor {

namespace {

using Kind = ExprNode::Kind;

Expr unary(Kind kind, const Expr& a) {
    return ExprAccess::make(ExprNode{kind, 0.0, {}, ExprAccess::share(a), nullptr});
}

Expr binary(Kind kind, const Expr& a, const Expr& b) {
    return ExprAccess::make(ExprNode{kind, 0.0, {}, ExprAccess::share(a), ExprAccess::share(b)});
}

} // namespace

Expr::Expr() : Expr(0.0) {}
Expr::Expr(int value) : Expr(static_cast<double>(value)) {}
Expr::Expr(double value)
    : node_(std::make_shared<const ExprNode>(ExprNode{Kind::Number, value, {}, nullptr, nullptr})) {}
Expr::Expr(Param parameter)
    : node_(std::make_shared<const ExprNode>(ExprNode{Kind::Parameter, 0.0, parameter, nullptr, nullptr})) {}
Expr::Expr(std::shared_ptr<const ExprNode> node) : node_(std::move(node)) {}

std::optional<double> asNumber(const Expr& expr) {
    const ExprNode& node = ExprAccess::node(expr);
    if (node.kind == Kind::Number) return node.number;
    return std::nullopt;
}

Expr operator+(const Expr& a, const Expr& b) { return binary(Kind::Add, a, b); }
Expr operator-(const Expr& a, const Expr& b) { return binary(Kind::Subtract, a, b); }
Expr operator*(const Expr& a, const Expr& b) { return binary(Kind::Multiply, a, b); }
Expr operator/(const Expr& a, const Expr& b) { return binary(Kind::Divide, a, b); }
Expr operator-(const Expr& a) {
    // The negative of a plain number is a plain number, not a formula.
    if (const std::optional<double> number = asNumber(a)) return Expr(-*number);
    return unary(Kind::Negate, a);
}

Expr min(const Expr& a, const Expr& b) { return binary(Kind::Min, a, b); }
Expr max(const Expr& a, const Expr& b) { return binary(Kind::Max, a, b); }
Expr abs(const Expr& a) { return unary(Kind::Abs, a); }
Expr clamp(const Expr& value, const Expr& low, const Expr& high) { return min(max(value, low), high); }

} // namespace twintailor
