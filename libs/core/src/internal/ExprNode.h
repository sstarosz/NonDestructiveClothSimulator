#pragma once
// Internal: how an Expr is stored. Included only by core/*.cpp.

#include <memory>

#include "twintailor/core/Expr.h"

namespace twintailor {

struct ExprNode {
    enum class Kind { Number, Parameter, Add, Subtract, Multiply, Divide, Negate, Min, Max, Abs };

    Kind kind = Kind::Number;
    double number = 0.0;  // Kind::Number
    Param parameter;      // Kind::Parameter
    std::shared_ptr<const ExprNode> a, b;
};

struct ExprAccess {
    static const ExprNode& node(const Expr& e) { return *e.node_; }
    static const std::shared_ptr<const ExprNode>& share(const Expr& e) { return e.node_; }
    static Expr make(ExprNode node) { return Expr(std::make_shared<const ExprNode>(std::move(node))); }
};

} // namespace twintailor
