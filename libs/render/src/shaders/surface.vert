#version 450
// Triangles and lines alike: a point in space to the picture.
layout(push_constant) uniform Push {
    mat4 viewProjection;
    vec4 colour;
    vec4 toLight;
} push;

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 0) out vec3 seenNormal;

void main() {
    gl_Position = push.viewProjection * vec4(position, 1.0);
    seenNormal = normal;
}
