struct ObjectFrame {
    mat4 current;
    mat4 previous;
    uvec4 mesh;
};

layout(set = 0, binding = 3, std430) readonly buffer Objects {
    ObjectFrame frames[];
} objects;
