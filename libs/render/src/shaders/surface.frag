#version 450
// Lit from the light's direction. Cloth has two sides: a side facing away
// is lit as if it faced the light, a little darker so the inside reads.
// A surface that is not lit (toLight.w is 1: the gizmo) keeps its colour flat.
layout(push_constant) uniform Push {
    mat4 viewProjection;
    vec4 colour;
    vec4 toLight;
} push;

layout(location = 0) in vec3 seenNormal;
layout(location = 0) out vec4 result;

void main() {
    if (push.toLight.w > 0.5) {
        result = push.colour;
        return;
    }
    float facing = dot(normalize(seenNormal), push.toLight.xyz);
    float light = 0.28 + 0.72 * abs(facing) * (facing < 0.0 ? 0.8 : 1.0);
    result = vec4(push.colour.rgb * light, push.colour.a);
}
