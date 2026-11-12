#pragma once
// A numeric field: a number, a parameter, or a formula over them.
//
//   Expr a = 4_cm;
//   Expr b = waist;              // waist is a Param
//   Expr c = waist / 4 + 1_cm;
//
// An Expr is an immutable value that refers to parameters by handle, so
// there are no names to mistype and renaming a parameter breaks nothing.
// How it is stored is private to the core.

#include <memory>

#include "core/Ids.h"
#include "core/Units.h"

namespace clothcad {

struct ExprNode;

class Expr {
public:
    Expr();  // 0
    Expr(int value);
    Expr(double value);
    Expr(Param parameter);

private:
    friend struct ExprAccess;
    explicit Expr(std::shared_ptr<const ExprNode> node);
    std::shared_ptr<const ExprNode> node_;
};

Expr operator+(const Expr& a, const Expr& b);
Expr operator-(const Expr& a, const Expr& b);
Expr operator*(const Expr& a, const Expr& b);
Expr operator/(const Expr& a, const Expr& b);
Expr operator-(const Expr& a);

Expr min(const Expr& a, const Expr& b);
Expr max(const Expr& a, const Expr& b);
Expr abs(const Expr& a);
Expr clamp(const Expr& value, const Expr& low, const Expr& high);

} // namespace clothcad
