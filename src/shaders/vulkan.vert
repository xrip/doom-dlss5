#version 450
#extension GL_GOOGLE_include_directive : require
#include "objects.glsl"

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 previousPosition;
layout(location = 2) in vec2 texCoord;
layout(location = 3) in vec2 material;
layout(set = 0, binding = 2, std140) uniform Frame {
    mat4 currentMatrix;
    mat4 previousMatrix;
    vec4 cameraPosition;
    vec4 extent;
    vec4 jitter;
} frame;
layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec3 worldPosition;
layout(location = 2) flat out vec2 fragMaterial;
layout(location = 3) out vec4 currentClip;
layout(location = 4) out vec4 previousClip;
layout(location = 5) flat out uint fragObject;

void main() {
    fragTexCoord = texCoord;
    vec3 currentPosition = position;
    vec3 priorPosition = previousPosition;
    fragMaterial = material;
    fragObject = uint(gl_InstanceIndex);
    if (fragObject != 0u) {
        ObjectFrame object = objects.frames[fragObject];
        currentPosition = (object.current * vec4(position, 1.0)).xyz;
        priorPosition = (object.previous * vec4(position, 1.0)).xyz;
        fragMaterial = vec2(float(object.mesh.w), (object.mesh.z & 1u) != 0u ? 3.0 :
            (object.mesh.z & 2u) != 0u ? 2.0 : 0.0);
    }
    worldPosition = currentPosition;
    vec4 rasterClip = frame.currentMatrix * vec4(currentPosition, 1.0);
    currentClip = rasterClip;
    previousClip = frame.previousMatrix * vec4(priorPosition, 1.0);
    if (material.y > 0.5 && material.y < 1.5) {
        vec3 rayDirection = position - frame.cameraPosition.xyz;
        currentClip = frame.currentMatrix * vec4(rayDirection, 0.0);
        previousClip = frame.previousMatrix * vec4(rayDirection, 0.0);
    }
    currentClip.xy += frame.jitter.xy * currentClip.w;
    previousClip.xy += frame.jitter.zw * previousClip.w;
    rasterClip.xy += frame.jitter.xy * rasterClip.w;
    gl_Position = rasterClip;
}
