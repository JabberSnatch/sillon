R"_(
layout(std140, set = 0, binding = 0) uniform MatrixBlock {
    mat4 projection;
    mat4 view_model;
    mat4 view;
    vec2 screen_offset;
    ivec2 screen_size;
};

layout(location = 0) out vec3 iFS_volume_coords;
layout(location = 1) out vec3 iFS_camera_position;

void main() {
    vec3 vertex = CurrentVertex();
    gl_Position = projection * view_model * vec4(vertex, 1.0);

    iFS_camera_position = (inverse(view_model) * vec4(vec3(0.0), 1.0)).xyz;
    iFS_volume_coords = vertex * 0.5 + vec3(0.5);
}
)_"
