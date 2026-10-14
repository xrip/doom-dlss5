#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 worldPosition;
uniform sampler2D texture0;
uniform sampler2D texture1;
uniform vec3 cameraPosition;
out vec4 finalColor;
void main() {
    vec2 uv = fragTexCoord;
    int shade = int(round(fragColor.r * 255.0));
    if (fragColor.g > 0.5) {
        vec3 direction = normalize(worldPosition - cameraPosition);
        uv = vec2(atan(direction.z, direction.x) / 6.2831853 * 4.0,
                  clamp(100.0 / 128.0 - direction.y / max(length(direction.xz), 0.0001) * 1.25, 0.0, 0.999));
        shade = 0;
    }
    vec4 sampleColor = texture(texture0, uv);
    if (sampleColor.a < 0.5) discard;
    int index = int(round(sampleColor.r * 255.0));
    finalColor = texelFetch(texture1, ivec2(index, clamp(shade, 0, 31)), 0);
}
