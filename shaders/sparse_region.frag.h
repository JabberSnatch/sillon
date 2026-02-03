R"(

#extension GL_EXT_nonuniform_qualifier: enable

layout(location = 0) in vec3 iFS_volume_coords;
layout(location = 1) in vec3 iFS_camera_position;
layout(location = 0) out vec4 color;

layout(std140, set = 0, binding = 0) uniform MatrixBlock {
    mat4 projection;
    mat4 view_model;
    mat4 view;
    vec2 screen_offset;
    ivec2 screen_size;
};

layout(push_constant, std430) uniform PushConstants {
    vec3 light_dir;

    float display_mode;
    float node_size;
    int stack_depth;
    uint sample_offset;
};

layout(set = 1, binding = 0) uniform usampler3D masks[];
layout(set = 2, binding = 0) uniform usampler3D offsets[];

#if 0
#define NODE_SIZE 32.0
#define CHUNK_SIZE 4.0
#define STACK_SIZE 3
#define MAX_DEPTH STACK_SIZE
#endif

#define CHUNK_SIZE 4.0
#define STACK_SIZE 5


float uint2float(uint i) {return uintBitsToFloat(0x3F800000u | (i >> 9)) - 1.0;}
vec2 uint2float(uvec2 i) {return vec2(uint2float(i.x), uint2float(i.y));}
uint bitfieldReverse(uint x) {
    x = ((x & 0xAAAAAAAAu) >> 1) | ((x & 0x55555555u) << 1);
    x = ((x & 0xCCCCCCCCu) >> 2) | ((x & 0x33333333u) << 2);
    x = ((x & 0xF0F0F0F0u) >> 4) | ((x & 0x0F0F0F0Fu) << 4);
    x = ((x & 0xFF00FF00u) >> 8) | ((x & 0x00FF00FFu) << 8);
    return (x >> 16) | (x << 16);
}
uint interleave(uint x) {
    x &= 0x0000FFFFu;
    x = (x | (x << 8)) & 0x00FF00FFu;
    x = (x | (x << 4)) & 0x0F0F0F0Fu;
    x = (x | (x << 2)) & 0x33333333u;
    x = (x | (x << 1)) & 0x55555555u;
    return x;
}
uint morton2D(uvec2 p) {return interleave(p.x) | (interleave(p.y) << 1);}
uint OwenHash4(uint x, uint seed) {x = bitfieldReverse(x); x ^= x * 0x3D20ADEAu; x ^= (x >> 1) & (x << 1) & 0x55555555u; x += seed; x *= (seed >> 16) | 1u; x ^= (x >> 1) & (x << 1) & 0x55555555u; x ^= x * 0x05526c56u; x ^= x * 0x53a22864u; return 0xFFFF0000u & x;}
uvec2 plasticSequence(uint i) {return i * uvec2(3242174889u, 2447445414u);}
vec2 OwenPlastic(uint i, uint seed) {return uint2float(plasticSequence(bitfieldReverse(OwenHash4(i, seed ^ 0xF8ADE99Au))));}
mat3 genTB(vec3 N) {
     float s = N.z < 0.0 ? -1.0 : 1.0;
     float a = -1.0 / (s + N.z);
     float b = N.x * N.y * a;
     return mat3(
         vec3(1.0 + s * N.x * N.x * a, s * b, -s * N.x),
         vec3(b, s + N.y * N.y * a, -N.y),
         N);
 }

vec3 RegionTraversal0(vec3 org, vec3 dir)
{
    uvec2 data = uvec2(0);
    uint current_tex = 0;
    DDAContext ctx = InitDDAContext(org, dir, node_size);
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
    DDAContext ctx = InitDDAContext(org, dir, node_size);
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

    DDAContext ctx = InitDDAContext(org, dir, node_size);

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

            org = RecursionPointDDAContext(ctx, org, dir, node_size);
            ctx = InitDDAContext(org, dir, node_size);
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
            ctx = RestoreDDAContext(ctx_cache, dir, node_size);
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

    DDAContext ctx = InitDDAContext(org, dir, node_size);

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

                    org = RecursionPointDDAContext(ctx, org, dir, node_size);
                    ctx = InitDDAContext(org, dir, node_size);
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
                ctx = RestoreDDAContext(ctx_cache, dir, node_size);
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
    DDAContext ctx = InitDDAContext(org, dir, node_size);
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
                    if (depth < stack_depth)
                    {
                        ctx_cache[depth] = SaveDDAContext(ctx);
                        org_cache[depth] = org;
                        tex_index[depth] = current_tex;

                        //current_tex = current_tex;

                        org = RecursionPointDDAContext(ctx, org, dir, node_size);
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
            else if (depth > stack_depth)
            {
                float level_size = 1.0;
                for (uint index = 0; index < stack_depth; index++)
                {
                    float voxel_count = ((index & 1) != 0 ? CHUNK_SIZE : node_size);
                    t += DDAContextSaveT(ctx_cache[index], dir, voxel_count) * level_size;
                    level_size /= voxel_count;
                }
                t += HitTDDAContext(ctx) * level_size;
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
                    if (depth < stack_depth)
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
                        ctx = InitDDAContext(org, dir, node_size);
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
                    ctx = RestoreDDAContext(ctx_cache[depth], dir, node_size);
                    org = org_cache[depth];
                    current_tex = tex_index[depth];
                    StepDDAContext(ctx);
                }
                else
                    break;
            }
            else if (depth > stack_depth)
            {
                float level_size = 1.0;
                for (uint index = 0; index < stack_depth; index++)
                {
                    float voxel_count = ((index & 1) != 0 ? CHUNK_SIZE : node_size);
                    t += DDAContextSaveT(ctx_cache[index], dir, voxel_count) * level_size;
                    level_size /= voxel_count;
                }
                t += HitTDDAContext(ctx) * level_size;
                break;
            }
        }
    }

    if (hit)
        return vec4(output_value, t);
    else
        return vec4((HitNormalDDAContext(ctx) * 0.5 + vec3(0.5)) * 0.1, -1.0);
}
)"

R"(
void main()
{
    vec2 uv =
        (vec2(gl_FragCoord.xy) - vec2(screen_offset.xy))
        / vec2(screen_size.xy);
    uint pixelID = morton2D(uvec2(gl_FragCoord.xy));
    uint pixelCount = uint(screen_size.x) * uint(screen_size.y);

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

#if 0
        color.xyz = frag_depth.xxx;
        color.w = 1.0;
        return;

#else
        float fog = exp(-frag_depth * 10.0);

        vec3 hitp = org + dir*trav.w;
        float shadow = 0.0;
        float diffuse = 0.0;

        float indirect = 0.0;
        vec3 avg_dir = vec3(0.0);
        float avg_t = 0.0;
        if (display_mode == 0.0)
        {
            shadow = RegionTraversal4(hitp + light_dir*0.0001, light_dir).w >= 0.0 ? 0.0 : 1.0;
            diffuse = max(0.0, dot(light_dir, trav.xyz));
        }
        else
        {
            vec4 previous_hit = trav;
            vec3 previous_org = org;
            vec3 previous_dir = dir;
            float current_weight = 1.0;

#if 1
#define SAMPLE_COUNT 16
            for (uint sample_index = 0; sample_index < SAMPLE_COUNT; ++sample_index)
            {
                vec3 previous_hitp = previous_org + previous_dir*previous_hit.w;
                vec3 current_org = previous_hitp + previous_hit.xyz*0.0001;
                if (dot(previous_hit.xyz, light_dir) > 0.0)
                {
                    float sample_t = RegionTraversal4(current_org + light_dir*0.0001, light_dir).w;
                    float sample_v = sample_t >= 0.0 ? 0.0 : 1.0;
                    indirect += 1.0 * sample_v
                        * max(0.0, dot(previous_hit.xyz, light_dir))
                        * current_weight;
                }

                vec2 owen_sample = OwenPlastic(
                    pixelCount * SAMPLE_COUNT * sample_offset +
                    pixelID * SAMPLE_COUNT + sample_index, 0);

                float sqrt_term = sqrt(1.0 - owen_sample.x*owen_sample.x);
                float phi = owen_sample.y * 3.14159265 * 2.0;
                vec3 ts_sample = vec3(
                    cos(phi) * sqrt_term,
                    sin(phi) * sqrt_term,
                    owen_sample.x
                );

                mat3 tb_mat = genTB(trav.xyz);
                vec3 current_ray = tb_mat * ts_sample;
                float next_weight = max(0.0, dot(-previous_dir, current_ray));

                if (dot(current_ray, previous_hit.xyz) < 0.0)
                    break;

                vec4 current_hit = RegionTraversal4(current_org, current_ray);
                if (current_hit.w < 0.0)
                    break;

                previous_org = current_org;
                previous_dir = current_ray;
                previous_hit = current_hit;
                current_weight *= next_weight;
            }

#else

#define SAMPLE_COUNT 4
            for (uint sample_index = 0; sample_index < SAMPLE_COUNT; ++sample_index)
            {
                vec2 owen_sample = OwenPlastic(
                    pixelCount * SAMPLE_COUNT * sample_offset +
                    pixelID * SAMPLE_COUNT + sample_index, 0);
                float theta = owen_sample.x * 3.14159265 * 0.5;
                float phi = owen_sample.y * 3.14159265 * 2.0;

                vec3 ts_sample = vec3(
                    cos(theta) * cos(phi),
                    cos(theta) * sin(phi),
                    sin(theta)
                    );

                mat3 tb_mat = genTB(trav.xyz);
                vec3 second_ray = tb_mat * ts_sample;

                avg_dir += second_ray / float(SAMPLE_COUNT);
                if (dot(second_ray, trav.xyz) <= 0.0)
                    continue;

                vec4 second_hit = RegionTraversal4(hitp + trav.xyz*0.0001, second_ray);
                if (second_hit.w < 0.0)
                    continue;

                vec3 second_hitp = hitp + second_ray*second_hit.w;
                if (dot(second_hit.xyz, light_dir) <= 0.0)
                    continue;

                {
                    float sample_t = RegionTraversal4(second_hitp + light_dir*0.0001, light_dir).w;
                    float sample_v = sample_t >= 0.0 ? 0.0 : 1.0;
                    avg_dir += second_ray / float(SAMPLE_COUNT);
                    indirect += (sample_v * max(0.0, dot(-second_ray, light_dir))) / float(SAMPLE_COUNT)
;
                    avg_t += sample_v;
                }
            }
#endif

        }

        //color.xyz = vec3(mix(shadow.xxx * diffuse + indirect.xxx * indirect_contribution, vec3(0.3, 0.5, 0.5), (1.0 - fog).xxx));
        if (display_mode < 2.0)
            color.xyz = shadow.xxx * diffuse + indirect.xxx;
#if 0
        else if (display_mode == 2.0)
            color.xyz = avg_t.xxx/16.0;
        else if (display_mode == 3.0)
            color.xyz = dot(trav.xyz, light_dir).xxx;
        else if (display_mode == 4.0)
            color.xyz = avg_dir * 0.5 + vec3(0.5);
#endif

        //color.xyz *= light_dir * 0.25 + vec3(1.5);
        //color.x = shadow * diffuse;
        //color.y = 1.0 - step(indirect * indirect_contribution, 0.0);
        //color.z = 0.0;
        //color.xyz = indirect.xxx * indirect_contribution;
#endif
    }
    else
        color.xyz = light_dir * 0.5 + vec3(0.5) + trav.xyz;//trav.xyz;
    color.w = 1.0;
}

)"
