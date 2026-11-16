// Stands in for app/Texture.cpp in tests, where there is no graphics card:
// the picture is accepted and dropped.
#include "Texture.h"

namespace clothcad::app {

void Texture::set(const std::uint32_t*, int width, int height) {
    name_ = 1;
    width_ = width;
    height_ = height;
}

} // namespace clothcad::app
