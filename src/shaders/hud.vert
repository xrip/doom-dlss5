#version 450

layout(location = 0) in vec3 position;
layout(location = 4) in vec4 vertexColor;
layout(set = 0, binding = 2, std140) uniform Frame {
    mat4 currentMatrix;
    mat4 previousMatrix;
    vec4 cameraPosition;
    vec4 extent;
    vec4 jitter;
} frame;
layout(location = 0) out vec4 color;

void main() {
    gl_Position = vec4(position.xy / frame.extent.zw * 2.0 - 1.0, 0.0, 1.0);
    color = vertexColor;
}
