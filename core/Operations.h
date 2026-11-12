#pragma once
// The operation types. One struct per type; each declares the edges and
// corners it creates and a Names struct that turns them into accessors.
//
// Curve, Line, Cut, Mirror, Instance ... are added here, and to OpData, as
// they are built.

#include <concepts>
#include <cstdint>
#include <variant>

#include "core/Expr.h"
#include "core/Ids.h"

namespace clothcad {

// Starting operation: an axis-aligned rectangle with its bottom-left corner
// at the panel's origin.
struct Rect {
    Expr width;
    Expr height;

    enum class Edge : uint16_t { Bottom, Right, Top, Left };
    enum class Corner : uint16_t { BottomLeft, BottomRight, TopRight, TopLeft };

    struct Names {
        OpId id;
        EdgeRef bottom() const { return {id, static_cast<uint16_t>(Edge::Bottom)}; }
        EdgeRef right() const { return {id, static_cast<uint16_t>(Edge::Right)}; }
        EdgeRef top() const { return {id, static_cast<uint16_t>(Edge::Top)}; }
        EdgeRef left() const { return {id, static_cast<uint16_t>(Edge::Left)}; }
        CornerRef bottomLeft() const { return {id, static_cast<uint16_t>(Corner::BottomLeft)}; }
        CornerRef bottomRight() const { return {id, static_cast<uint16_t>(Corner::BottomRight)}; }
        CornerRef topRight() const { return {id, static_cast<uint16_t>(Corner::TopRight)}; }
        CornerRef topLeft() const { return {id, static_cast<uint16_t>(Corner::TopLeft)}; }
    };
};

// Starting operation: a piece of a ring, as used for a circle skirt. You give
// the length of the inner edge (the waist) and the app works out the radius.
//
//   RingSector{.innerLength = waist, .sweep = 360_deg * fullness, .depth = skirtLength}
//
// The ring's centre is at the panel's origin and the sector hangs downward,
// symmetric about the vertical axis. A full circle (sweep = 360_deg) is the
// same four edges with the two sides lying on top of each other, so the
// names do not change with the sweep.
struct RingSector {
    Expr innerLength;  // arc length of the inner edge
    Expr sweep;        // angle covered, greater than 0 and at most 360_deg
    Expr depth;        // distance from the inner edge to the outer edge

    enum class Edge : uint16_t { Inner, Outer, StartSide, EndSide };
    enum class Corner : uint16_t { InnerStart, InnerEnd, OuterStart, OuterEnd };

    struct Names {
        OpId id;
        EdgeRef inner() const { return {id, static_cast<uint16_t>(Edge::Inner)}; }
        EdgeRef outer() const { return {id, static_cast<uint16_t>(Edge::Outer)}; }
        EdgeRef startSide() const { return {id, static_cast<uint16_t>(Edge::StartSide)}; }
        EdgeRef endSide() const { return {id, static_cast<uint16_t>(Edge::EndSide)}; }
        CornerRef innerStart() const { return {id, static_cast<uint16_t>(Corner::InnerStart)}; }
        CornerRef innerEnd() const { return {id, static_cast<uint16_t>(Corner::InnerEnd)}; }
        CornerRef outerStart() const { return {id, static_cast<uint16_t>(Corner::OuterStart)}; }
        CornerRef outerEnd() const { return {id, static_cast<uint16_t>(Corner::OuterEnd)}; }
    };
};

using OpData = std::variant<Rect, RingSector>;

// True only for the exact types listed in OpData.
template <class T, class Variant>
inline constexpr bool isAlternative = false;
template <class T, class... Ts>
inline constexpr bool isAlternative<T, std::variant<Ts...>> = (std::same_as<T, Ts> || ...);

template <class T>
concept OperationType = isAlternative<T, OpData>;

} // namespace clothcad
