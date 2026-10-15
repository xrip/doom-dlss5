#version 450
#extension GL_GOOGLE_include_directive : require
#include "objects.glsl"

layout(location = 0) in vec3 position;
layout(location = 2) in vec2 texCoord;
layout(set = 0, binding = 2, std140) uniform Frame {
    mat4 currentMatrix;
    mat4 previousMatrix;
    vec4 cameraPosition;
    vec4 extent;
    vec4 jitter;
} frame;
layout(location = 0) out vec2 fragTexCoord;

void main() {
    fragTexCoord = texCoord;
    vec4 world = vec4(position, 1.0);
    if (gl_InstanceIndex != 0) world = objects.frames[gl_InstanceIndex].current * world;
    vec4 clip = frame.currentMatrix * world;
    clip.xy += frame.jitter.xy * clip.w;
    gl_Position = clip;
}
