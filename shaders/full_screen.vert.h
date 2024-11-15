R"_(
const vec3 kTriVertices[] = vec3[3](
    vec3(-1.0, -1.0, 1.0),
    vec3(-1.0, 4.0, 1.0),
    vec3(4.0, -1.0, 1.0)
);
layout(location = 0) out vec3 iFS_csPosition;
void main()
{
    iFS_csPosition = kTriVertices[gl_VertexIndex];
    gl_Position = vec4(kTriVertices[gl_VertexIndex], 1.0);
}
)_"
