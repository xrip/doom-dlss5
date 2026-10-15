layout(set = 1, binding = 0) uniform accelerationStructureEXT worldAS;
layout(set = 1, binding = 1) uniform sampler2D shadowTextures[];
layout(set = 1, binding = 2, std140) uniform RayLighting {
    uint64_t vertices;
    uint64_t materials;
    uvec4 info;
    vec4 positions[4];
    vec4 colors[4];
    vec4 sunDirection;
    vec4 sunColor;
} rayLighting;

layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer RayVertexFloats {
    float values[];
};

layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer RayMaterials {
    uint values[];
};

vec3 decodePaletteSrgb(vec3 value) {
    vec3 low = value / 12.92;
    vec3 high = pow((value + 0.055) / 1.055, vec3(2.4));
    return mix(high, low, lessThanEqual(value, vec3(0.04045)));
}

vec3 encodePaletteSrgb(vec3 value) {
    vec3 low = 12.92 * value;
    vec3 high = 1.055 * pow(value, vec3(1.0 / 2.4)) - 0.055;
    return mix(high, low, lessThanEqual(value, vec3(0.0031308)));
}

bool rayHasOccluder(vec3 origin, vec3 direction, float maxDistance) {
    if (maxDistance <= 0.001) return false;
    rayQueryEXT query;
    rayQueryInitializeEXT(query, worldAS, gl_RayFlagsTerminateOnFirstHitEXT, 0xff,
                          origin, 0.001, direction, maxDistance);
    RayVertexFloats vertices = RayVertexFloats(rayLighting.vertices);
    RayMaterials materials = RayMaterials(rayLighting.materials);
    while (rayQueryProceedEXT(query)) {
        if (rayQueryGetIntersectionTypeEXT(query, false) == gl_RayQueryCandidateIntersectionTriangleEXT) {
            uint primitive = rayQueryGetIntersectionPrimitiveIndexEXT(query, false);
            uint textureIndex = materials.values[primitive];
            if (textureIndex == 0xffffffffu || textureIndex >= rayLighting.info.w) continue;
            vec2 barycentrics = rayQueryGetIntersectionBarycentricsEXT(query, false);
            uint first = primitive * 3u * rayLighting.info.z;
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

vec4 applyRayLighting(vec4 oldColor, vec4 baseColor, vec3 position, vec3 normal) {
    vec3 baseLinear = decodePaletteSrgb(baseColor.rgb);
    vec3 litLinear = decodePaletteSrgb(oldColor.rgb) * 0.55;
    vec3 sunDirection = normalize(rayLighting.sunDirection.xyz);
    float sunAmount = max(dot(normal, sunDirection), 0.0) * rayLighting.sunDirection.w;
    if (sunAmount > 0.0 && !rayHasOccluder(position + normal * 0.003, sunDirection, 10000.0))
        litLinear += baseLinear * rayLighting.sunColor.rgb * sunAmount;
    for (uint lightIndex = 0u; lightIndex < min(rayLighting.info.x, 4u); ++lightIndex) {
        vec3 toLight = rayLighting.positions[lightIndex].xyz - position;
        float distanceToLight = length(toLight);
        float radius = rayLighting.positions[lightIndex].w;
        if (distanceToLight <= 0.001 || radius <= 0.001 || distanceToLight >= radius) continue;
        vec3 direction = toLight / distanceToLight;
        float lambert = max(dot(normal, direction), 0.0) * max(direction.y, 0.0);
        if (lambert <= 0.0 || rayHasOccluder(position + normal * 0.003, direction, distanceToLight - 0.003))
            continue;
        float fade = 1.0 - distanceToLight / radius;
        float attenuation = fade * fade / (1.0 + distanceToLight * distanceToLight);
        litLinear += baseLinear * rayLighting.colors[lightIndex].rgb *
                     (lambert * rayLighting.colors[lightIndex].w * attenuation);
    }
    return vec4(encodePaletteSrgb(clamp(litLinear, 0.0, 1.0)), oldColor.a);
}
