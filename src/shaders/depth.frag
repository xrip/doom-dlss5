#version 450

layout(set = 0, binding = 0) uniform sampler2D assetTexture;
layout(location = 0) in vec2 fragTexCoord;
#ifdef SPRITE_DEPTH_MERGE
layout(set = 1, binding = 0) uniform sampler2D spriteDepth;
#endif

void main() {
    if (texture(assetTexture, fragTexCoord).a < 0.5) discard;
#ifdef SPRITE_DEPTH_MERGE
    if (gl_FragCoord.z > texelFetch(spriteDepth, ivec2(gl_FragCoord.xy), 0).r) discard;
#endif
}
