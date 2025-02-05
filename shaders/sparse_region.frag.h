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

layout(set = 1, binding = 0) uniform usampler3D masks[];

struct NodePointer
{
    uint level;
    uint offset;
};

layout(std430, set = 0, binding = 0) buffer NodesBlock {
    NodePointer pointers[];
};

#define NODE_SIZE 2.0
#define CHUNK_SIZE 4.0
#define STACK_SIZE 1

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
        else if (depth == 1)
            break;
    }

    if (hit)
        return output_value;
    else
        return (HitNormalDDAContext(ctx) * 0.5 + vec3(0.5)) * 0.1;
}

vec3 RegionTraversal4(vec3 org, vec3 dir)
{
    uvec2 data = uvec2(0);
    uint current_tex = 0;

    int depth = 0;
    DDAContextSave ctx_cache[STACK_SIZE];
    vec3 org_cache[STACK_SIZE];

    bool hit = false;
    vec3 output_value = vec3(0.0);

    DDAContext ctx = InitDDAContext(org, dir, NODE_SIZE);
    while (true)
    {
        bool found_chunk = false;

        while (ctx.mask.w == 1.0)
        {
            data = texture(masks[nonuniformEXT(current_tex)], ctx.voxel_coords).xy;
            if (data != uvec2(0, 0))
            {
                if (depth != STACK_SIZE)
                {
                    ctx_cache[depth] = SaveDDAContext(ctx);
                    org_cache[depth] = org;
                    org = RecursionPointDDAContext(ctx, org, dir, NODE_SIZE);
                    ctx = InitDDAContext(org, dir, NODE_SIZE);
                    ctx.mask.xyz = ctx_cache[depth].mask.xyz;
                    ++depth;
                    break;
                }
                else
                {
                    int popcount = bitCount(data[0]) + bitCount(data[1]);
                    hit = true;
                    found_chunk = true;
                    output_value = vec3(float(popcount));
                    break;
                }
            }
            StepDDAContext(ctx);
        }

        if (!found_chunk)
        {
            if (depth > 0)
            {
                --depth;
                ctx = RestoreDDAContext(ctx_cache[depth], dir, NODE_SIZE);
                org = org_cache[depth];
                StepDDAContext(ctx);
            }
            else
                break;
        }

        if (hit)
            break;
    }

    if (hit)
        return output_value;
    else
        return (HitNormalDDAContext(ctx) * 0.5 + vec3(0.5)) * 0.1;
}

vec3 RegionTraversal(vec3 org, vec3 dir)
{
    int depth = 0;
    uint current_tex = 0;
    DDAContextSave ctx_cache[STACK_SIZE];
    vec3 org_cache[STACK_SIZE];
    uint tex_index[STACK_SIZE];
    uvec2 data_stack[STACK_SIZE];

vec3 output_value = vec3(0.0);
        bool hit = false;
    DDAContext ctx = InitDDAContext(org, dir, NODE_SIZE);
    while (true) // entire stack
    {
        if ((depth & 1) == 0)
        {
            bool found_bit = false;

            while (ctx.mask.w == 1.0)
            {
                data_stack[depth] = texture(masks[nonuniformEXT(current_tex)], ctx.voxel_coords).xy;

                if (data_stack[depth] != uvec2(0, 0))
                {
                    found_bit = true;
                    ctx_cache[depth] = SaveDDAContext(ctx);
                    org_cache[depth] = org;
                    org = RecursionPointDDAContext(ctx, org, dir, NODE_SIZE);
                    ctx = InitDDAContext(org, dir, CHUNK_SIZE);
                    ctx.mask.xyz = ctx_cache[depth].mask.xyz;
                    tex_index[depth] = current_tex;
                    data_stack[depth+1] = data_stack[depth];
                    ++depth;
                    break;
                }

                StepDDAContext(ctx);
            }

            if (!found_bit)
            {
                if (depth > 0)
                {
                    --depth;
                    current_tex = tex_index[depth];
                    ctx = RestoreDDAContext(ctx_cache[depth], dir, CHUNK_SIZE);
                    org = org_cache[depth];
                    StepDDAContext(ctx);
                }
                else
                    break;
            }
        }
        else
        {
            bool found_chunk = false;

            while (ctx.mask.w == 1.0)
            {
                uvec3 int_coords = uvec3(floor(ctx.voxel_coords * CHUNK_SIZE));
                uint side_index = int_coords.z / 2;
                uint voxel_index = int_coords.x + int_coords.y * 4 + (int_coords.z%2)*16;

                if ((data_stack[depth][side_index] & (1<<voxel_index)) != 0)
                {
                    if (depth != STACK_SIZE)
                    {
                        found_chunk = true;
                        ctx_cache[depth] = SaveDDAContext(ctx);
                        org_cache[depth] = org;
                        org = RecursionPointDDAContext(ctx, org, dir, CHUNK_SIZE);
                        ctx = InitDDAContext(org, dir, NODE_SIZE);
                        ctx.mask.xyz = ctx_cache[depth].mask.xyz;

                        tex_index[depth] = current_tex;
                        current_tex = 1;//int_coords.x + int_coords.z * 8;

                        ++depth;
                        break;
                    }
                    else
{

                int popcount = 0;
                if (side_index > 0)
                    popcount = bitCount(data_stack[depth][0]);
                popcount += bitCount(data_stack[depth][side_index] & ((1<<voxel_index)-1));

                        //return vec3(float(popcount) / 64.0) * (dir * 0.5 + vec3(0.5));
// * ctx.voxel_coords;
                        //return vec3(float(popcount));// * (dir * 0.5 + vec3(0.5));
                        //return vec3(float(popcount)) - vec3(float(popcount)) + vec3(1.0, 0.0, 1.0);
                        //return ctx.voxel_coords;
                        //return HitNormalDDAContext(ctx);
hit = true;
output_value = vec3(float(popcount)) * (dir * 0.5 + vec3(0.5));
//return output_value;
break;
}
                }

                StepDDAContext(ctx);
            }

if (hit) break;

            if (!found_chunk)
            {
                if (depth > 0)
                {
                    --depth;
                    current_tex = tex_index[depth];
                    ctx = RestoreDDAContext(ctx_cache[depth], dir, NODE_SIZE);
                    org = org_cache[depth];
                    StepDDAContext(ctx);
                }
                else
                    break;
            }

        }
    }

if (hit)
return output_value;
else
    return HitNormalDDAContext(ctx) * 0.1;
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

    vec3 trav = RegionTraversal3(org, dir);
    color.xyz = trav;// * 0.5 + vec3(0.5);
    color.w = 1.0;
}

)"
