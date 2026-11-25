#version 450
// Lines take their colour as it is: they are not lit.
layout(push_constant) uniform Push {
    mat4 viewProjection;
    vec4 colour;
    vec4 toLight;
    vec4 back;
} push;

layout(location = 0) out vec4 result;

void main() { result = push.colour; }
