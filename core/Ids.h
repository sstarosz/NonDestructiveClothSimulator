#pragma once
// Strong handles. Each is a distinct type, so a panel can't be passed where
// a parameter is expected. The document hands them out; ids are unique in a
// document and never reused.

#include <cstdint>

namespace clothcad {

struct Param {
    uint32_t value = 0;
    friend bool operator==(Param, Param) = default;
};

struct PanelId {
    uint32_t value = 0;
    friend bool operator==(PanelId, PanelId) = default;
};

struct OpId {
    uint32_t value = 0;
    friend bool operator==(OpId, OpId) = default;
};

// A named edge or corner: the operation that created it plus that
// operation's own role enum (Rect::Edge::Top, ...), stored as a number.
// Later operations and queries refer to these, never to indices or strings.
struct EdgeRef {
    OpId op;
    uint16_t role = 0;
    friend bool operator==(EdgeRef, EdgeRef) = default;
};

struct CornerRef {
    OpId op;
    uint16_t role = 0;
    friend bool operator==(CornerRef, CornerRef) = default;
};

// An OpId that remembers its operation type. It inherits the operation's
// Names, so an OpRef<Rect> has .top(), .left(), .topLeft() ... and nothing
// a rectangle doesn't have.
template <class T>
struct OpRef : T::Names {
    OpRef() = default;
    explicit OpRef(OpId i) { this->id = i; }
    operator OpId() const { return this->id; }
};

} // namespace clothcad
