R"_(

layout(location = 0) in vec3 iFS_csPosition;
layout(location = 0) out vec4 oFS_color;

layout(set = 0, binding = 0) uniform sampler2D framebuffer;

layout(push_constant, std430) uniform PushConstant {
    ivec2 screen_size;
};

void main()
{
    ivec2 frag_coord = ivec2(gl_FragCoord.xy);
    vec2 uv =
        (vec2(
            (frag_coord/2)
            + (screen_size.xy / 2)*ivec2(frag_coord.x&1, frag_coord.y&1)) + vec2(0.5))
        / vec2(screen_size.xy);
    oFS_color = vec4(texture(framebuffer, uv).xyz, 1.0);
}

)_"
