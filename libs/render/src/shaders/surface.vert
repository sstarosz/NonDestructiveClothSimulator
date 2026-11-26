#version 450
// A point of a triangle: from space to the picture.
layout(push_constant) uniform Push {
    mat4 viewProjection;
    vec4 colour;
    vec4 toLight;
    vec4 back;
} push;

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 0) out vec3 seenNormal;
// Drawn twice, once for its depth alone, a surface must land on the very same depths.
invariant gl_Position;

void main() {
    gl_Position = push.viewProjection * vec4(position, 1.0);
    seenNormal = normal;
}
