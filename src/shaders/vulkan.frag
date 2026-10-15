#version 460

#ifdef RAY_LIGHTING
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_ray_query : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#endif

layout(set = 0, binding = 0) uniform sampler2D assetTexture;
layout(set = 0, binding = 1) uniform sampler2D paletteTexture;
layout(set = 0, binding = 2, std140) uniform Frame {
    mat4 currentMatrix;
    mat4 previousMatrix;
    vec4 cameraPosition;
    vec4 extent;
    vec4 jitter;
} frame;
layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 worldPosition;
layout(location = 2) flat in vec2 fragMaterial;
layout(location = 3) in vec4 currentClip;
layout(location = 4) in vec4 previousClip;
layout(location = 0) out vec4 color;
layout(location = 1) out vec2 motion;

#ifdef RAY_LIGHTING
#include "lighting.glsl"
#endif

void main() {
    motion = vec2(0.0);
    if (currentClip.w > 0.00001 && previousClip.w > 0.00001)
        motion = (currentClip.xy / currentClip.w - previousClip.xy / previousClip.w) * 0.5;
    vec2 uv = fragTexCoord;
    int shade = int(round(fragMaterial.x));
    bool sky = fragMaterial.y > 0.5 && fragMaterial.y < 1.5;
    if (sky) {
        vec3 direction = normalize(worldPosition - frame.cameraPosition.xyz);
        uv = vec2(atan(direction.z, direction.x) / 6.2831853 * 4.0,
                  clamp(100.0 / 128.0 - direction.y / max(length(direction.xz), 0.0001) * 1.25, 0.0, 0.999));
        shade = 0;
    }
#ifdef RAY_LIGHTING
    vec3 normalCross = cross(dFdy(worldPosition), dFdx(worldPosition));
    float normalLength = dot(normalCross, normalCross);
    vec3 worldNormal = normalLength > 1e-12 ? normalCross * inversesqrt(normalLength) : vec3(0.0, 1.0, 0.0);
#endif
    vec4 sampleColor = texture(assetTexture, uv);
    if (sampleColor.a < 0.5) discard;
    int index = int(round(sampleColor.r * 255.0));
    color = texelFetch(paletteTexture, ivec2(index, clamp(shade, 0, 31)), 0);
#ifdef RAY_LIGHTING
    if (rayLighting.info.y != 0u && !sky && fragMaterial.y != 2.0)
        color = applyRayLighting(color, texelFetch(paletteTexture, ivec2(index, 0), 0), worldPosition, worldNormal);
#endif
}
