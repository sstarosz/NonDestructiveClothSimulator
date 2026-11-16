#pragma once
// Strong handles. Each is a distinct type, so a panel can't be passed where
// a parameter is expected. The document hands them out; ids are unique in a
// document and never reused.

#include <cstdint>
#include <vector>

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

struct SeamId {
    uint32_t value = 0;
    friend bool operator==(SeamId, SeamId) = default;
};

// A named edge or corner: the operation that created it plus that
// operation's own role enum (Rect::Edge::Top, ...), stored as a number.
// Later operations and queries refer to these, never to indices or strings.
//
// `via` is empty for the original. A mirror adds its own id, so the mirrored
// copy of rect.top() is a different name: mirror.image(rect.top()).
struct EdgeRef {
    OpId op;
    uint16_t role = 0;
    std::vector<OpId> via{};
    friend bool operator==(const EdgeRef&, const EdgeRef&) = default;
};

struct CornerRef {
    OpId op;
    uint16_t role = 0;
    std::vector<OpId> via{};
    friend bool operator==(const CornerRef&, const CornerRef&) = default;
};

// A named internal line, created by a Line operation.
struct LineRef {
    OpId op;
    friend bool operator==(LineRef, LineRef) = default;
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
