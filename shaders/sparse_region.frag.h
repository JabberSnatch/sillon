R"(

#extension GL_EXT_nonuniform_qualifier: enable

layout(location = 0) in vec3 iFS_volume_coords;
layout(location = 1) in vec3 iFS_camera_position;
layout(location = 0) out vec4 color;

layout(std140, set = 0, binding = 1) uniform MatrixBlock {
    mat4 projection;
    mat4 view_model;
    mat4 view;
    vec2 screen_offset;
    ivec2 screen_size;
};

//layout(set = 0, binding = 1) uniform usampler3D region;

#if 1
layout(set = 0, binding = 2) uniform usampler3D masks[];
struct NodePointer
{
    uint level;
    uint offset;
};
layout(std430, set = 0, binding = 0) buffer NodesBlock {
    NodePointer pointers[];
};
#endif

vec3 RegionTraversal(vec3 org, vec3 dir)
{
    DDAContextSave ctx_cache[3];
    vec3 org_cache[3];

    DDAContext ctx = InitDDAContext(org, dir, 128.0);
    while (ctx.mask.w == 1.0)
    {
        uvec2 data = texture(masks[0], ctx.voxel_coords).xy;

        if (data != uvec2(0, 0))
        {
            #if 1
            ctx_cache[0] = SaveDDAContext(ctx);
            org_cache[0] = org;
            org = RecursionPointDDAContext(ctx, org, dir, 128.0);
            ctx = InitDDAContext(org, dir, 4.0);
            ctx.mask.xyz = ctx_cache[0].mask.xyz;

            while (ctx.mask.w == 1.0)
            {
                uvec3 int_coords = uvec3(floor(ctx.voxel_coords * 4.0));
                uint side_index = int_coords.z / 2;
                uint voxel_index = int_coords.x + int_coords.y * 4 + (int_coords.z%2)*16;

                if ((data[side_index] & (1<<voxel_index)) != 0)
                    return HitNormalDDAContext(ctx);

                StepDDAContext(ctx);
            }

            ctx = RestoreDDAContext(ctx_cache[0], dir, 128.0);
            org = org_cache[0];

            #else
            return HitNormalDDAContext(ctx);
            #endif
        }

        StepDDAContext(ctx);
    }

    return vec3(0.0);
}

void main()
{
    vec2 uv =
        (vec2(gl_FragCoord.xy) - vec2(screen_offset.xy))
        / vec2(screen_size.xy);

    vec3 dir =
        inverse(mat3(view_model)) *
        inverse(mat3(projection))
        * vec3(uv * 2.0 - vec2(1.0), 1.0);
    dir = normalize(dir);

    vec3 step_sign = sign(-dir);
    vec3 max_t = abs(((step_sign * 0.5 + vec3(0.5)) - (step_sign * iFS_volume_coords)) / dir);

    float depth = distance(iFS_camera_position * 0.5 + vec3(0.5), iFS_volume_coords);
    float region_begin = min(max_t.x, min(max_t.y, max_t.z));

    vec3 root_org = iFS_volume_coords
        - dir * 0.00001
        - (dir * min(depth, region_begin));

    depth = max(depth - region_begin, 0.0);

    vec3 org = root_org;

    vec3 trav = RegionTraversal(org, dir);
    color.xyz = trav * 0.5 + vec3(0.5);
    color.w = 1.0;
}

)"
