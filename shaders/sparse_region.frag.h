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

layout(push_constant, std430) uniform PushConstants {
    vec3 light_dir;
};

layout(set = 1, binding = 0) uniform usampler3D masks[];
layout(set = 2, binding = 0) uniform usampler3D offsets[];

#define NODE_SIZE 32.0
#define CHUNK_SIZE 4.0
#define STACK_SIZE 3

vec3 RegionTraversal0(vec3 org, vec3 dir)
{
    uvec2 data = uvec2(0);
    uint current_tex = 0;
    DDAContext ctx = InitDDAContext(org, dir, NODE_SIZE);
    while (ctx.mask.w == 1.0)
    {
        data = texture(masks[nonuniformEXT(current_tex)], ctx.voxel_coords).xy;
        if (data != uvec2(0, 0))
        {
            int popcount = bitCount(data[0]) + bitCount(data[1]);
            return vec3(float(popcount));
        }
        StepDDAContext(ctx);
    }
    return (HitNormalDDAContext(ctx) * 0.5 + vec3(0.5)) * 0.1;
}

vec3 RegionTraversal1(vec3 org, vec3 dir)
{
    uvec2 data = uvec2(0);
    uint current_tex = 0;
    DDAContext ctx = InitDDAContext(org, dir, NODE_SIZE);
    bool hit = false;
    vec3 output_value = vec3(0.0);
    while (ctx.mask.w == 1.0)
    {
        data = texture(masks[nonuniformEXT(current_tex)], ctx.voxel_coords).xy;
        if (data != uvec2(0, 0))
        {
            int popcount = bitCount(data[0]) + bitCount(data[1]);
            hit = true;
            output_value = vec3(float(popcount));
            break;
        }
        StepDDAContext(ctx);
    }

    if (hit)
        return output_value;
    else
        return (HitNormalDDAContext(ctx) * 0.5 + vec3(0.5)) * 0.1;
}

vec3 RegionTraversal2(vec3 org, vec3 dir)
{
    uvec2 data = uvec2(0);
    uint current_tex = 0;

    int depth = 0;
    DDAContextSave ctx_cache;
    vec3 org_cache;

    bool hit = false;
    vec3 output_value = vec3(0.0);

    DDAContext ctx = InitDDAContext(org, dir, NODE_SIZE);

    while (ctx.mask.w == 1.0)
    {
        data = texture(masks[nonuniformEXT(current_tex)], ctx.voxel_coords).xy;
        if (data != uvec2(0, 0))
        {
            int popcount = bitCount(data[0]) + bitCount(data[1]);
            hit = true;
            output_value = vec3(float(popcount));

            ctx_cache = SaveDDAContext(ctx);
            org_cache = org;

            org = RecursionPointDDAContext(ctx, org, dir, NODE_SIZE);
            ctx = InitDDAContext(org, dir, NODE_SIZE);
            ctx.mask.xyz = ctx_cache.mask.xyz;
            ++depth;

            hit = false;

            while (ctx.mask.w == 1.0)
            {
                data = texture(masks[nonuniformEXT(current_tex)], ctx.voxel_coords).xy;
                if (data != uvec2(0, 0))
                {
                    output_value = ctx.voxel_coords;
                    hit = true;
                    break;
                }
                StepDDAContext(ctx);
            }

            if (hit)
                break;

            --depth;
            ctx = RestoreDDAContext(ctx_cache, dir, NODE_SIZE);
            org = org_cache;
        }
        StepDDAContext(ctx);
    }

    if (hit)
        return output_value;
    else
        return (HitNormalDDAContext(ctx) * 0.5 + vec3(0.5)) * 0.1;
}

vec3 RegionTraversal3(vec3 org, vec3 dir)
{
    uvec2 data = uvec2(0);
    uint current_tex = 0;

    int depth = 0;
    DDAContextSave ctx_cache;
    vec3 org_cache;

    bool hit = false;
    vec3 output_value = vec3(0.0);

    DDAContext ctx = InitDDAContext(org, dir, NODE_SIZE);

    while (true)
    {
        hit = false;

        while (ctx.mask.w == 1.0)
        {
            data = texture(masks[nonuniformEXT(current_tex)], ctx.voxel_coords).xy;
            if (data != uvec2(0, 0))
            {
                hit = true;
                if (depth < 1)
                {
                    ctx_cache = SaveDDAContext(ctx);
                    org_cache = org;

                    org = RecursionPointDDAContext(ctx, org, dir, NODE_SIZE);
                    ctx = InitDDAContext(org, dir, NODE_SIZE);
                    ctx.mask.xyz = ctx_cache.mask.xyz;
                    ++depth;
                }
                else
                {
                    output_value = ctx.voxel_coords;
                    ++depth;
                }
                break;
            }
            StepDDAContext(ctx);
        }

        if (!hit)
        {
            if (depth > 0)
            {
                --depth;
                ctx = RestoreDDAContext(ctx_cache, dir, NODE_SIZE);
                org = org_cache;
                StepDDAContext(ctx);
            }
            else
                break;
        }
        else if (depth > 1)
            break;
    }

    if (hit)
        return output_value;
    else
        return (HitNormalDDAContext(ctx) * 0.5 + vec3(0.5)) * 0.1;
}

vec4 RegionTraversal4(vec3 org, vec3 dir)
{
    DDAContextSave ctx_cache[STACK_SIZE];
    vec3 org_cache[STACK_SIZE];
    uint tex_index[STACK_SIZE];
    uvec2 data[STACK_SIZE+1];

    bool hit = false;
    vec3 output_value = vec3(0.0);
    float t = 0.0;

    int depth = 0;
    DDAContext ctx = InitDDAContext(org, dir, NODE_SIZE);
    uint current_tex = 0;

    while (true)
    {
        hit = false;

        if ((depth & 1) == 0)
        {
            while (ctx.mask.w == 1.0)
            {
                data[depth] = texture(masks[nonuniformEXT(current_tex)], ctx.voxel_coords).xy;
                if (data[depth] != uvec2(0, 0))
                {
                    hit = true;
                    if (depth < STACK_SIZE)
                    {
                        ctx_cache[depth] = SaveDDAContext(ctx);
                        org_cache[depth] = org;
                        tex_index[depth] = current_tex;

                        //current_tex = current_tex;

                        org = RecursionPointDDAContext(ctx, org, dir, NODE_SIZE);
                        ctx = InitDDAContext(org, dir, CHUNK_SIZE);
                        ctx.mask.xyz = ctx_cache[depth].mask.xyz;
                        ++depth;
                    }
                    else
                    {
                        output_value = HitNormalDDAContext(ctx);
                        ++depth;
                    }
                    break;
                }
                StepDDAContext(ctx);
            }

            if (!hit)
            {
                if (depth > 0)
                {
                    --depth;
                    ctx = RestoreDDAContext(ctx_cache[depth], dir, CHUNK_SIZE);
                    org = org_cache[depth];
                    current_tex = tex_index[depth];
                    StepDDAContext(ctx);
                }
                else
                    break;
            }
            else if (depth > STACK_SIZE)
            {
                float level_size = 1.0;
                for (uint index = 0; index < STACK_SIZE; index++)
                {
                    float voxel_count = ((index & 1) != 0 ? CHUNK_SIZE : NODE_SIZE);
                    t += DDAContextSaveT(ctx_cache[index], dir, voxel_count) * level_size;
                    level_size /= voxel_count;
                }
                t += ctx.t * level_size;
                break;
            }
        }
        else
        {
            while (ctx.mask.w == 1.0)
            {
                uvec3 int_coords = uvec3(floor(ctx.voxel_coords * CHUNK_SIZE));
                uint side_index = int_coords.z / 2;
                uint voxel_index = int_coords.x + int_coords.y * 4 + (int_coords.z%2)*16;

                if ((data[depth-1][side_index] & (1<<voxel_index)) != 0)
                {
                    hit = true;
                    if (depth < STACK_SIZE)
                    {
                        ctx_cache[depth] = SaveDDAContext(ctx);
                        org_cache[depth] = org;
                        tex_index[depth] = current_tex;

                        uint offset = bitCount(data[depth-1][side_index] & ((1<<voxel_index)-1));
                        if (side_index == 1)
                            offset += bitCount(data[depth-1][0]);

                        current_tex = offset +
                            texture(offsets[nonuniformEXT(current_tex)],
                                    ctx_cache[depth-1].voxel_coords).x;

                        org = RecursionPointDDAContext(ctx, org, dir, CHUNK_SIZE);
                        ctx = InitDDAContext(org, dir, NODE_SIZE);
                        ctx.mask.xyz = ctx_cache[depth].mask.xyz;
                        ++depth;
                    }
                    else
                    {
                        output_value = HitNormalDDAContext(ctx);
                        ++depth;
                    }
                    break;
                }
                StepDDAContext(ctx);
            }

            if (!hit)
            {
                if (depth > 0)
                {
                    --depth;
                    ctx = RestoreDDAContext(ctx_cache[depth], dir, NODE_SIZE);
                    org = org_cache[depth];
                    current_tex = tex_index[depth];
                    StepDDAContext(ctx);
                }
                else
                    break;
            }
            else if (depth > STACK_SIZE)
            {
                float level_size = 1.0;
                for (uint index = 0; index < STACK_SIZE; index++)
                {
                    float voxel_count = ((index & 1) != 0 ? CHUNK_SIZE : NODE_SIZE);
                    t += DDAContextSaveT(ctx_cache[index], dir, voxel_count) * level_size;
                    level_size /= voxel_count;
                }
                t += ctx.t * level_size;
                break;
            }
        }
    }

    if (hit)
        return vec4(output_value, t);
    else
        return vec4((HitNormalDDAContext(ctx) * 0.5 + vec3(0.5)) * 0.1, -1.0);
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

    float entry_depth = distance(iFS_camera_position * 0.5 + vec3(0.5), iFS_volume_coords);
    float region_begin = min(max_t.x, min(max_t.y, max_t.z));

    vec3 root_org = iFS_volume_coords
        - dir * 0.00001
        - (dir * min(entry_depth, region_begin));

    entry_depth = max(entry_depth - region_begin, 0.0);

    vec3 org = root_org;

    vec4 trav = RegionTraversal4(org, dir);
    if (trav.w >= 0.0)
    {
        float frag_depth = trav.w + entry_depth;
        float fog = exp(-frag_depth * 10.0);

        vec3 hitp = org + dir*trav.w;
        float shadow = RegionTraversal4(hitp + light_dir*0.000001, light_dir).w > 0.0 ? 0.0 : 1.0;

        float diffuse = max(0.0, dot(light_dir, trav.xyz));

        color.xyz = vec3(mix(shadow.xxx * diffuse, vec3(0.3, 0.5, 0.5), (1.0 - fog).xxx));
    }
    else
        color.xyz = trav.xyz;
    color.w = 1.0;
}

)"
