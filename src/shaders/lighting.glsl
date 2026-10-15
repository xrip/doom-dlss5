layout(set = 1, binding = 0) uniform accelerationStructureEXT worldAS;
layout(set = 1, binding = 1) uniform sampler2D shadowTextures[];
struct RayLight {
    vec4 position;
    vec4 color;
    uvec4 meta;
};

layout(set = 1, binding = 2, std140) uniform RayLighting {
    uint64_t vertices;
    uint64_t materials;
    uvec4 info;
    uvec4 geometry;
    RayLight lights[32];
    vec4 sunDirection;
    vec4 sunColor;
} rayLighting;

layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer RayVertexFloats {
    float values[];
};

layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer RayMaterials {
    uint values[];
};

#include "objects.glsl"

#include "color.glsl"

bool rayHasOccluder(vec3 origin, vec3 direction, float maxDistance, uint emitter) {
    if (maxDistance <= 0.001) return false;
    rayQueryEXT query;
    rayQueryInitializeEXT(query, worldAS, gl_RayFlagsTerminateOnFirstHitEXT, 0xff,
                          origin, 0.001, direction, maxDistance);
    RayVertexFloats vertices = RayVertexFloats(rayLighting.vertices);
    RayMaterials materials = RayMaterials(rayLighting.materials);
    while (rayQueryProceedEXT(query)) {
        if (rayQueryGetIntersectionTypeEXT(query, false) == gl_RayQueryCandidateIntersectionTriangleEXT) {
            uint primitive = rayQueryGetIntersectionPrimitiveIndexEXT(query, false);
            uint objectId = rayQueryGetIntersectionInstanceCustomIndexEXT(query, false);
            uint textureIndex, firstVertex = 0u;
            if (objectId != 0u) {
                if (objectId == emitter || (fragMaterial.y != 3.0 && objectId == fragObject)) continue;
                ObjectFrame object = objects.frames[objectId];
                if ((object.mesh.z & 1u) != 0u) {
                    rayQueryConfirmIntersectionEXT(query);
                    continue;
                }
                textureIndex = object.mesh.y;
                firstVertex = object.mesh.x;
            } else {
                textureIndex = materials.values[primitive];
            }
            if (textureIndex == 0xffffffffu || textureIndex >= rayLighting.info.w) continue;
            vec2 barycentrics = rayQueryGetIntersectionBarycentricsEXT(query, false);
            uint first = (firstVertex + primitive * 3u) * rayLighting.info.z;
            vec2 uv0 = vec2(vertices.values[first + 6u], vertices.values[first + 7u]);
            vec2 uv1 = vec2(vertices.values[first + rayLighting.info.z + 6u],
                            vertices.values[first + rayLighting.info.z + 7u]);
            vec2 uv2 = vec2(vertices.values[first + 2u * rayLighting.info.z + 6u],
                            vertices.values[first + 2u * rayLighting.info.z + 7u]);
            vec2 hitUv = uv0 * (1.0 - barycentrics.x - barycentrics.y) +
                         uv1 * barycentrics.x + uv2 * barycentrics.y;
            float alpha = textureLod(shadowTextures[nonuniformEXT(textureIndex)], hitUv, 0.0).a;
            if (alpha >= 0.5) rayQueryConfirmIntersectionEXT(query);
        }
    }
    return rayQueryGetIntersectionTypeEXT(query, true) != gl_RayQueryCommittedIntersectionNoneEXT;
}

float rayRandom(vec2 offset) {
    return fract(sin(dot(gl_FragCoord.xy + offset, vec2(12.9898, 78.233)) +
                     float(rayLighting.geometry.z) * 37.719) * 43758.5453);
}

vec3 sampleEmitter(float radius, float seed) {
    if (rayLighting.geometry.y == 0u) return vec3(0.0);
    float height = rayRandom(vec2(seed, 17.0)) * 2.0 - 1.0;
    float angle = rayRandom(vec2(3.0, seed)) * 6.2831853;
    float ring = sqrt(max(0.0, 1.0 - height * height));
    return vec3(cos(angle) * ring, height, sin(angle) * ring) * radius;
}

vec3 rayLightingRadiance(vec4 oldColor, vec4 baseColor, vec3 position, vec3 normal) {
    vec3 baseLinear = decodePaletteSrgb(baseColor.rgb);
    vec3 litLinear = decodePaletteSrgb(oldColor.rgb) * 0.55;
    vec3 sunDirection = normalize(rayLighting.sunDirection.xyz + sampleEmitter(0.004, 91.0));
    float sunAmount = max(dot(normal, sunDirection), 0.0) * rayLighting.sunDirection.w;
    if (sunAmount > 0.0 && !rayHasOccluder(position + normal * 0.003, sunDirection, 10000.0, 0u))
        litLinear += baseLinear * rayLighting.sunColor.rgb * sunAmount;
    for (uint lightIndex = 0u; lightIndex < min(rayLighting.info.x, 32u); ++lightIndex) {
        RayLight light = rayLighting.lights[lightIndex];
        vec3 offset = sampleEmitter(light.meta.x != 0u ? 0.35 : 0.08, float(lightIndex) * 23.0);
        if (light.meta.x != 0u) offset.y = 0.0;
        vec3 toLight = light.position.xyz + offset - position;
        float distanceToLight = length(toLight);
        float radius = light.position.w;
        if (distanceToLight <= 0.001 || radius <= 0.001 || distanceToLight >= radius) continue;
        vec3 direction = toLight / distanceToLight;
        float lambert = max(dot(normal, direction), 0.0);
        if (light.meta.x != 0u) lambert *= max(direction.y, 0.0);
        if (lambert <= 0.0 || rayHasOccluder(position + normal * 0.003, direction, distanceToLight - 0.003, light.meta.y))
            continue;
        float fade = 1.0 - distanceToLight / radius;
        float attenuation = fade * fade / (1.0 + distanceToLight * distanceToLight);
        litLinear += baseLinear * light.color.rgb * (lambert * light.color.w * attenuation);
    }
    return litLinear;
}
