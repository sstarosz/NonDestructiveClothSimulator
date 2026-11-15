#pragma once
// A picture on the graphics card, for showing pixels we worked out
// ourselves inside the interface.

#include <cstdint>

#include <imgui.h>

namespace clothcad::app {

class Texture {
public:
    // Replaces the picture. `pixels` holds width * height colours in ImGui's
    // IM_COL32 layout, top row first.
    void set(const std::uint32_t* pixels, int width, int height);

    // What to hand to ImGui to draw it. Meaningless before the first set().
    ImTextureID id() const { return static_cast<ImTextureID>(name_); }
    bool empty() const { return width_ == 0; }

private:
    unsigned int name_ = 0;  // the graphics card's name for it; it lives as long as the window
    int width_ = 0, height_ = 0;
};

} // namespace clothcad::app
