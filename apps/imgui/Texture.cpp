#include "Texture.h"

// Only the oldest part of OpenGL is needed here, which every system's own
// header declares.
#include <GLFW/glfw3.h>

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_UNPACK_ROW_LENGTH
#define GL_UNPACK_ROW_LENGTH 0x0CF2
#endif

namespace clothcad::app {

void Texture::set(const std::uint32_t* pixels, int width, int height) {
    if (width <= 0 || height <= 0) return;
    GLint boundBefore = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &boundBefore);
    if (name_ == 0) {
        GLuint name = 0;
        glGenTextures(1, &name);
        name_ = name;
    }
    glBindTexture(GL_TEXTURE_2D, name_);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    if (width != width_ || height != height_) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        width_ = width;
        height_ = height;
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    }
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(boundBefore));
}

} // namespace clothcad::app
