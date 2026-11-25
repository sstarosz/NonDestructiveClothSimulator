#version 450
// A point of a line. A line with a lift is drawn that many cm nearer the eye,
// along its own line of sight: it lands on the same spot of the picture, but a
// surface it lies on does not cut it.
layout(push_constant) uniform Push {
    mat4 viewProjection;
    vec4 colour;
    vec4 eyeAndLift;
    vec4 back;
} push;

layout(location = 0) in vec3 position;

void main() {
    vec3 drawn = position;
    if (push.eyeAndLift.w > 0.0) drawn += normalize(push.eyeAndLift.xyz - position) * push.eyeAndLift.w;
    gl_Position = push.viewProjection * vec4(drawn, 1.0);
}
