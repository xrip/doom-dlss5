#version 450

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

void main() {
    fragTexCoord = texCoord;
    worldPosition = position;
    fragMaterial = material;
    vec4 rasterClip = frame.currentMatrix * vec4(position, 1.0);
    currentClip = rasterClip;
    previousClip = frame.previousMatrix * vec4(previousPosition, 1.0);
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
