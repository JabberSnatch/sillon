#version 450

#extension GL_GOOGLE_include_directive: enable
#extension GL_EXT_nonuniform_qualifier: enable

#include "voxel_traversal.glsl"
#include "sampling.glsl"

#include "atmosphere_parameters.glsl"

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

layout(std430, set = 0, binding = 1) buffer ResidencyBlock {
    uint residency_mask[];
};

layout(set = 0, binding = 2) uniform sampler2D skybox;
layout(set = 0, binding = 3) uniform sampler2D trlut;

layout(push_constant, std430) uniform PushConstants {
    vec3 light_dir;

    float display_mode;
    vec3 region_scale;
    float node_size;
    int stack_depth;
    uint sample_offset;
    uint leaves_begin;
    float sun_power;
    int precomputed_sky;
    float light_phi;
};

layout(set = 1, binding = 0) uniform usampler3D masks[];
layout(set = 2, binding = 0) uniform usampler3D offsets[];
layout(set = 3, binding = 0) uniform sampler3D payloads[];

#if 0
#define NODE_SIZE 32.0
#define CHUNK_SIZE 4.0
#define STACK_SIZE 3
#define MAX_DEPTH STACK_SIZE
#endif

#define CHUNK_SIZE 4.0
#define STACK_SIZE 5

#define LOG_CHUNK_SIZE 2

//#define FLOAT_CONTEXT
#define FIXED_CONTEXT
//#define INLINE_RESULTS
#define TRAVERSAL_BASE

float StepCheck(FixedPointDDA fix, DDAContext floating)
{
    if (fix.mask.w != floating.mask.w)
        return 0.0;

    ivec3 current_voxel_fixed = FixedPointDDACurrentVoxel(fix);

    vec3 test0 = floating.voxel_coords / abs(floating.voxel_step);
    ivec3 current_voxel_floating = ivec3(floor(floating.voxel_coords / abs(floating.voxel_step)));

    ivec3 diff = abs(current_voxel_fixed - current_voxel_floating);
    if (min(diff, ivec3(1)) != diff)
        return 0.0;
    return 1.0;
}

#if 0
#define CHECK_INTERRUPT \
    if (StepCheck(alt_ctx, ctx) == 0.0) { payload = vec3(0.0 / 0.0); return vec4(vec3(0.0 / 0.0), 1.0); }
#else

#define CHECK_INTERRUPT {}

#endif

float IterationCheck(inout int iteration_count)
{
    if (iteration_count++ > 4096)
        return 0.0;
    return 1.0;
}

#if 0

#define ITERATION_CONTROL \
    if (IterationCheck(iteration_count) == 0.0) { payload = vec3(0.0 / 0.0); return vec4(1.0); }

#define ITERATION_CONTROL_BOOL \
    if (IterationCheck(iteration_count) == 0.0) { error = (0.0/0.0); return true; }

#else

#define ITERATION_CONTROL {}
#define ITERATION_CONTROL_BOOL {}

#endif

#ifdef FLOAT_CONTEXT
#define HITP_TYPE vec3
#else
#define HITP_TYPE ivec3
#endif

// uvw
// hit_p
// normal

struct TraversalContext
{
    uvec2 depth_state;

#ifdef FLOAT_CONTEXT
    DDAContext ref_dda;
    DDAContextSave ref_dda_cache[STACK_SIZE];
#endif

#ifdef FIXED_CONTEXT
    FixedPointDDA dda;
    FixedPointDDABackup dda_cache[STACK_SIZE];
#endif
};

#ifdef FIXED_CONTEXT

#define RUN_CONDITION ctx.dda.mask.w == 1
#define TEXDATA_LOOKUP                                                 \
    texelFetch(masks[nonuniformEXT(tex_data[depth_index].x)],          \
               FixedPointDDACurrentVoxel(ctx.dda), 0).xy
#define BASE_OFFSET_LOOKUP                                              \
    texelFetch(offsets[nonuniformEXT(tex_data[depth_index].x)],         \
               FixedPointDDABackupCurrentVoxel(ctx.dda_cache[ctx.depth_state.x-1]), 0).x
#define CURRENT_VOXEL_LOOKUP FixedPointDDACurrentVoxel(ctx.dda)
#define BACKUP_HITT(INDEX)                                              \
    FixedPointDDABackupHitT(ctx.dda_cache[INDEX], ctx.dda.direction) * level_size
#define HITT FixedPointDDAHitT(ctx.dda) * level_size
#define HITNORMAL FixedPointDDAHitNormal(ctx.dda)

#define FIXED_RECURSION(TGT_SIZE)                               \
    ctx.dda_cache[ctx.depth_state.x] = FixedPointDDASave(ctx.dda);  \
    ctx.dda = FixedPointDDARecursion(ctx.dda, dir, TGT_SIZE)
#define FIXED_STEP FixedPointDDAStep(ctx.dda)
#define FIXED_RESTORE(TGT_SIZE)                 \
    ctx.dda = FixedPointDDARestore(ctx.dda_cache[ctx.depth_state.x], dir, TGT_SIZE)

#else

#define RUN_CONDITION ctx.ref_dda.mask.w == 1.0
#define TEXDATA_LOOKUP                                              \
    texture(masks[nonuniformEXT(tex_data[depth_index].x)],          \
            TextureCoordsDDAContext(ctx.ref_dda)).xy
#define BASE_OFFSET_LOOKUP                                              \
    texture(offsets[nonuniformEXT(tex_data[depth_index].x)],            \
            TextureCoordsDDAContextSave(ctx.ref_dda_cache[ctx.depth_state.x-1])).x
#define CURRENT_VOXEL_LOOKUP VoxelCoordsDDAContext(ctx.ref_dda)
#define BACKUP_HITT(INDEX)                                              \
    HitTDDAContextSave(ctx.ref_dda_cache[INDEX], dir, voxel_count) * level_size
#define HITT HitTDDAContext(ctx.ref_dda) * level_size
#define HITNORMAL HitNormalDDAContext(ctx.ref_dda)

#define FIXED_RECURSION(TGT_SIZE) {}
#define FIXED_STEP {}
#define FIXED_RESTORE(TGT_SIZE) {}

#endif

#ifdef FLOAT_CONTEXT

#define FLOAT_RECURSION(SRC_SIZE, TGT_SIZE)                             \
    ctx.ref_dda_cache[ctx.depth_state.x] = SaveDDAContext(ctx.ref_dda);     \
    org_cache[ctx.depth_state.x] = org;                                     \
    org = RecursionPointDDAContext(ctx.ref_dda, org, dir, SRC_SIZE);    \
    ctx.ref_dda = InitDDAContext(org, dir, TGT_SIZE);                   \
    ctx.ref_dda.mask.xyz = ctx.ref_dda_cache[ctx.depth_state.x].mask.xyz
#define FLOAT_STEP StepDDAContext(ctx.ref_dda)
#define FLOAT_RESTORE(TGT_SIZE)                                         \
    ctx.ref_dda = RestoreDDAContext(ctx.ref_dda_cache[ctx.depth_state.x], dir, TGT_SIZE); \
    org = org_cache[ctx.depth_state.x]

#else

#define FLOAT_RECURSION(SRC_SIZE, TGT_SIZE) {}
#define FLOAT_STEP {}
#define FLOAT_RESTORE(TGT_SIZE) {}

#endif

bool RegionTraversal_Base(out TraversalContext ctx, ivec3 org, ivec3 dir)
{
    bool hit = false;

    int iteration_count = 0;
    uvec3 tex_data[(STACK_SIZE+1) / 2];

    ctx.depth_state = uvec2(0, stack_depth);

    int log_node_size = int(log(node_size) / log(2));
    ctx.dda = FixedPointDDAInit_Base(org, dir, log_node_size);

    tex_data[0].x = 0;
    while (true)
    {
        ITERATION_CONTROL_BOOL;
        hit = false;
        uint depth_index = ctx.depth_state.x >> 1;

        if ((ctx.depth_state.x & 1) == 0)
        {
            while (RUN_CONDITION)
            {
                ITERATION_CONTROL_BOOL;

                tex_data[depth_index].yz = TEXDATA_LOOKUP;
                if (tex_data[depth_index].yz != uvec2(0, 0))
                {
                    hit = true;

                    if (ctx.depth_state.x < ctx.depth_state.y)
                    {
                        FLOAT_RECURSION(node_size, CHUNK_SIZE);
                        FIXED_RECURSION(LOG_CHUNK_SIZE);
                    }

                    ++ctx.depth_state.x;

                    break;
                }
                else
                {
                    FLOAT_STEP;
                    FIXED_STEP;
                    CHECK_INTERRUPT;
                }
            }

            if (!hit)
            {
                if (ctx.depth_state.x > 0)
                {
                    --ctx.depth_state.x;

                    FLOAT_RESTORE(CHUNK_SIZE);
                    FLOAT_STEP;

                    FIXED_RESTORE(LOG_CHUNK_SIZE);
                    FIXED_STEP;

                    CHECK_INTERRUPT;
                }
                else break; // no hit
            }
            else if (ctx.depth_state.x > ctx.depth_state.y) break; // hit
        }

        else // ((ctx.depth_state.x & 1) != 0)
        {
            while (RUN_CONDITION)
            {
                ITERATION_CONTROL_BOOL;

                uvec3 int_coords = CURRENT_VOXEL_LOOKUP;
                uvec2 indices = // (side_index, voxel_index)
                    uvec2(int_coords.z / 2,
                          int_coords.x + int_coords.y * 4 + (int_coords.z%2) * 16);

                if ((tex_data[depth_index][indices.x+1] & (1 << indices.y)) != 0)
                {
                    hit = true;

                    if (ctx.depth_state.x < ctx.depth_state.y)
                    {
                        bool next_tex_missing = true;

                        uint base_offset = BASE_OFFSET_LOOKUP;
                        if (base_offset != 0xffffffff)
                        {
                            uint offset =
                                base_offset
                                + bitCount(
                                    tex_data[depth_index][indices.x+1]
                                    & ((1 << indices.y)-1))
                                + indices.x * bitCount(tex_data[depth_index][1]);

                            tex_data[depth_index+1].x = offset;
                            next_tex_missing =
                                (residency_mask[offset >> 5] >> (offset & 0x1fu) & 1u) == 0;
                        }

                        if (next_tex_missing)
                        {
                            ctx.depth_state.y = ctx.depth_state.x;
                            ctx.depth_state.x = stack_depth+1;
                            break;
                        }

                        FLOAT_RECURSION(CHUNK_SIZE, node_size);
                        FIXED_RECURSION(log_node_size);
                    }

                    ++ctx.depth_state.x;

                    break;
                }
                else
                {
                    FLOAT_STEP;
                    FIXED_STEP;
                    CHECK_INTERRUPT;
                }
            }

            if (!hit)
            {
                if (ctx.depth_state.x > 0)
                {
                    --ctx.depth_state.x;

                    FLOAT_RESTORE(node_size);
                    FLOAT_STEP;

                    FIXED_RESTORE(log_node_size);
                    FIXED_STEP;

                    CHECK_INTERRUPT;
                }
                else break; // no hit (probably an error)
            }
            else if (ctx.depth_state.x > ctx.depth_state.y) break; // hit
        }
    }

    return hit;
}

ivec4 TraversalContext_Flatten(TraversalContext ctx)
{
    ivec4 voxel_coordinates;
    uint max_index = min(ctx.depth_state.y, stack_depth);
    voxel_coordinates.xyz = FixedPointDDACurrentVoxel(ctx.dda);
    voxel_coordinates.w = ctx.dda.log_size;
    for (uint index = 0; index < max_index; index++)
    {
        uint rev_index = max_index - 1 - index;
        voxel_coordinates.xyz =
            voxel_coordinates.xyz +
            (FixedPointDDABackupCurrentVoxel(ctx.dda_cache[rev_index]) << voxel_coordinates.w);

        int log_level_size = ctx.dda_cache[rev_index].log_size;
        voxel_coordinates.w += log_level_size;
    }
    return voxel_coordinates;
}

void RegionTraversal_HitData(inout ivec3 org, inout ivec3 dir, ivec4 voxel_coordinates)
{
    int voxel_size = FIXED_PRECISION >> voxel_coordinates.w;
    ivec3 hitp = FixedPointDDAHitP_Base(org, dir, voxel_coordinates.xyz, voxel_size, -1);

    ivec3 offset = abs(hitp - (voxel_coordinates.xyz * voxel_size + voxel_size / 2));
    ivec3 normal = ivec3(
        greaterThanEqual(offset, ivec3(max(offset.x, max(offset.y, offset.z)))));

    dir = -normal * sign(dir);
    org = hitp;
}

bool RegionTraversal_Query(inout TraversalContext ctx, ivec3 org, ivec3 dir)
{
    return RegionTraversal_Base(ctx, org, dir);
}

//bool RegionTraversal_Fixed(inout ivec3 org, inout ivec3 dir)
bool RegionTraversal_Fixed(inout TraversalContext ctx, inout ivec3 org, inout ivec3 dir)
{
    //TraversalContext ctx;
    bool hit = false;
    hit = RegionTraversal_Base(ctx, org, dir);

    if (hit)
    {
        ivec4 voxel_coordinates = TraversalContext_Flatten(ctx);

#if 0
        int voxel_size = FIXED_PRECISION >> voxel_coordinates.w;
        ivec3 hitp = FixedPointDDAHitP_Base(org, dir, voxel_coordinates.xyz, voxel_size, -1);

        ivec3 offset = abs(hitp - (voxel_coordinates.xyz * voxel_size + voxel_size / 2));
        ivec3 normal = ivec3(
            greaterThanEqual(offset, ivec3(max(offset.x, max(offset.y, offset.z)))));

        dir = -normal * sign(dir);
        org = hitp;
#endif
        RegionTraversal_HitData(org, dir, voxel_coordinates);
    }

    return hit;
}

bool RegionTraversal_Fixed(inout TraversalContext ctx,
                           inout ivec3 org, inout ivec3 dir, out vec2 uv)
{
    bool hit = false;
    hit = RegionTraversal_Base(ctx, org, dir);

    if (hit)
    {
        ivec4 voxel_coordinates = TraversalContext_Flatten(ctx);
        RegionTraversal_HitData(org, dir, voxel_coordinates);

        int voxel_size = FIXED_PRECISION >> voxel_coordinates.w;
        ivec3 uv_mask = 1 - abs(dir);
        ivec3 uvw = (org & (voxel_size-1)) * uv_mask;
        ivec2 fuv = (uv_mask.x == 0) ? uvw.yz : ((uv_mask.y == 0) ? uvw.xz : uvw.xy);
        uv = FixedToFloat(fuv << voxel_coordinates.w);
    }
    else
        uv = vec2(0.0);

    return hit;
}

vec4 RegionTraversal4(vec3 org, vec3 dir, inout vec3 payload)
{
#ifdef TRAVERSAL_BASE
    TraversalContext ctx;
    float error = 1.0;

#if 1
    bool hit = false;

    {
        int iteration_count = 0;
        uvec3 tex_data[(STACK_SIZE+1) / 2];

        ctx.depth_state = uvec2(0, stack_depth);

#ifdef FLOAT_CONTEXT
        vec3 org_cache[STACK_SIZE];
        ctx.ref_dda = InitDDAContext(org, dir, node_size);
#endif

#ifdef FIXED_CONTEXT
        int log_node_size = int(log(node_size) / log(2));
        ctx.dda = FixedPointDDAInit(org, dir, log_node_size);
#endif

        tex_data[0].x = 0;
        while (true)
        {
            ITERATION_CONTROL_BOOL;
            hit = false;
            uint depth_index = ctx.depth_state.x >> 1;

            if ((ctx.depth_state.x & 1) == 0)
            {
                while (RUN_CONDITION)
                {
                    ITERATION_CONTROL_BOOL;

                    tex_data[depth_index].yz = TEXDATA_LOOKUP;
                    if (tex_data[depth_index].yz != uvec2(0, 0))
                    {
                        hit = true;

                        if (ctx.depth_state.x < stack_depth)
                        {
                            FLOAT_RECURSION(node_size, CHUNK_SIZE);
                            FIXED_RECURSION(LOG_CHUNK_SIZE);
                        }

                        ++ctx.depth_state.x;

                        break;
                    }
                    else
                    {
                        FLOAT_STEP;
                        FIXED_STEP;
                        CHECK_INTERRUPT;
                    }
                }

                if (!hit)
                {
                    if (ctx.depth_state.x > 0)
                    {
                        --ctx.depth_state.x;

                        FLOAT_RESTORE(CHUNK_SIZE);
                        FLOAT_STEP;

                        FIXED_RESTORE(LOG_CHUNK_SIZE);
                        FIXED_STEP;

                        CHECK_INTERRUPT;
                    }
                    else break; // no hit
                }
                else if (ctx.depth_state.x > stack_depth) break; // hit
            }

            else // ((ctx.depth_state.x & 1) != 0)
            {
                while (RUN_CONDITION)
                {
                    ITERATION_CONTROL_BOOL;

                    uvec3 int_coords = CURRENT_VOXEL_LOOKUP;
                    uvec2 indices = // (side_index, voxel_index)
                        uvec2(int_coords.z / 2,
                              int_coords.x + int_coords.y * 4 + (int_coords.z%2) * 16);

                    if ((tex_data[depth_index][indices.x+1] & (1 << indices.y)) != 0)
                    {
                        hit = true;

                        if (ctx.depth_state.x < stack_depth)
                        {
                            bool next_tex_missing = true;

                            uint base_offset = BASE_OFFSET_LOOKUP;
                            if (base_offset != 0xffffffff)
                            {
                                uint offset =
                                    base_offset
                                    + bitCount(
                                        tex_data[depth_index][indices.x+1]
                                        & ((1 << indices.y)-1))
                                    + indices.x * bitCount(tex_data[depth_index][1]);

                                tex_data[depth_index+1].x = offset;
                                next_tex_missing =
                                    (residency_mask[offset >> 5] >> (offset & 0x1fu) & 1u) == 0;
                            }

                            if (next_tex_missing)
                            {
                                ctx.depth_state.y = ctx.depth_state.x;
                                ctx.depth_state.x = stack_depth+1;
                                break;
                            }

                            FLOAT_RECURSION(CHUNK_SIZE, node_size);
                            FIXED_RECURSION(log_node_size);
                        }

                        ++ctx.depth_state.x;

                        break;
                    }
                    else
                    {
                        FLOAT_STEP;
                        FIXED_STEP;
                        CHECK_INTERRUPT;
                    }
                }

                if (!hit)
                {
                    if (ctx.depth_state.x > 0)
                    {
                        --ctx.depth_state.x;

                        FLOAT_RESTORE(node_size);
                        FLOAT_STEP;

                        FIXED_RESTORE(log_node_size);
                        FIXED_STEP;

                        CHECK_INTERRUPT;
                    }
                    else break; // no hit (probably an error)
                }
                else if (ctx.depth_state.x > stack_depth) break; // hit
            }
        }
    }
#endif

    if (hit)
    {
        if (error != error)
            return vec4(vec3(error), 1.0);

        float t = 0.0;
        vec3 output_value = vec3(0.0);

#ifdef FIXED_CONTEXT
        uint max_index = min(ctx.depth_state.y, stack_depth);
        ivec3 voxel_coordinates = FixedPointDDACurrentVoxel(ctx.dda);
        int total_log_size = ctx.dda.log_size;
        for (uint index = 0; index < max_index; index++)
        {
            uint rev_index = max_index - 1 - index;
            voxel_coordinates =
                voxel_coordinates +
                (FixedPointDDABackupCurrentVoxel(ctx.dda_cache[rev_index]) << total_log_size);

            int log_level_size = ctx.dda_cache[rev_index].log_size;
            total_log_size += log_level_size;
        }

        ivec3 forg = clamp(FloatToFixed(org), 0, (FIXED_PRECISION-1));
        ivec3 fdir = ivec3(sign(dir)) * min(abs(FloatToFixed(dir)), FIXED_PRECISION-1);
        int voxel_size = FIXED_PRECISION >> total_log_size;
        ivec3 hitp = FixedPointDDAHitP_Base(forg, fdir, voxel_coordinates, voxel_size, -1);

        ivec3 offset = abs(hitp - (voxel_coordinates * voxel_size + voxel_size / 2));
        ivec3 normal = ivec3(
            greaterThanEqual(offset, ivec3(max(offset.x, max(offset.y, offset.z)))));

        vec3 ray = FixedToFloat(hitp - forg);
        t = length(ray);
        output_value = vec3(-normal * sign(fdir));

#else

        float level_size = 1.0;
        for (uint index = 0; index < min(ctx.depth_state.y, stack_depth); index++)
        {
            float voxel_count = ((index & 1) != 0 ? CHUNK_SIZE : node_size);
            t += BACKUP_HITT(index);
            level_size /= voxel_count;
        }

        t += HITT;
        output_value = HITNORMAL;
#endif

#if 0
        vec3 chunk_coords =
            (vec3(int_coords) + vec3(0.5)) / (CHUNK_SIZE * node_size);
        vec3 texture_coords = TextureCoordsDDAContextSave(ctx_cache[depth-1]);

        payload = texture(payloads[nonuniformEXT(current_tex - leaves_begin)],
                          texture_coords - vec3(0.5)/node_size + chunk_coords).xyz;
#endif

        payload = vec3(1.0);

        return vec4(output_value, t);
    }
    else
        return vec4(vec3(0.0), -1.0);

#else

    int iteration_count = 0;

    uint tex_index[STACK_SIZE];
    uvec2 data[STACK_SIZE+1];

    bool hit = false;

#ifdef INLINE_RESULTS
    vec3 output_value = vec3(0.0);
    float t = 0.0;
#endif

    uint depth_bypass = stack_depth;

    int depth = 0;
    uint current_tex = 0;

#ifdef FLOAT_CONTEXT
    DDAContext ctx = InitDDAContext(org, dir, node_size);
    DDAContextSave ctx_cache[STACK_SIZE];
    vec3 org_cache[STACK_SIZE];
#endif

#ifdef FIXED_CONTEXT
    int log_node_size = int(log(node_size) / log(2));
    FixedPointDDA alt_ctx = FixedPointDDAInit(org, dir, log_node_size);
    FixedPointDDABackup alt_cache[STACK_SIZE];
#endif

    while (true)
    {
        ITERATION_CONTROL;

        hit = false;

        if ((depth & 1) == 0)
        {
            while (
#ifdef FIXED_CONTEXT
                alt_ctx.mask.w == 1.0
#else
                ctx.mask.w == 1.0
#endif
            )
            {
                ITERATION_CONTROL;

#ifdef FIXED_CONTEXT
                data[depth] = texture(masks[nonuniformEXT(current_tex)],
                                      FixedPointDDATextureCoords(alt_ctx)).xy;
#else
                data[depth] = texture(masks[nonuniformEXT(current_tex)],
                                      TextureCoordsDDAContext(ctx)).xy;
#endif

                if (data[depth] != uvec2(0, 0))
                {
                    hit = true;

                    if (depth < stack_depth)
                    {
                        tex_index[depth] = current_tex;

#ifdef FLOAT_CONTEXT
                        ctx_cache[depth] = SaveDDAContext(ctx);
                        org_cache[depth] = org;
                        org = RecursionPointDDAContext(ctx, org, dir, node_size);
                        ctx = InitDDAContext(org, dir, CHUNK_SIZE);
                        ctx.mask.xyz = ctx_cache[depth].mask.xyz;
#endif

#ifdef FIXED_CONTEXT
                        alt_cache[depth] = FixedPointDDASave(alt_ctx);
                        alt_ctx = FixedPointDDARecursion(alt_ctx, dir, LOG_CHUNK_SIZE);
#endif

                        ++depth;
                    }
                    else
                    {
#ifdef INLINE_RESULTS
#ifdef FIXED_CONTEXT
                        output_value = FixedPointDDAHitNormal(alt_ctx);
#else
                        output_value = HitNormalDDAContext(ctx);
#endif

                        payload = vec3(1.0);
#endif

                        ++depth;
                    }

                    break;
                }

#ifdef FLOAT_CONTEXT
                StepDDAContext(ctx);
#endif

#ifdef FIXED_CONTEXT
                FixedPointDDAStep(alt_ctx);
#endif

                CHECK_INTERRUPT;
            }

            if (!hit)
            {
                if (depth > 0)
                {
                    --depth;

                    current_tex = tex_index[depth];

#ifdef FLOAT_CONTEXT
                    ctx = RestoreDDAContext(ctx_cache[depth], dir, CHUNK_SIZE);
                    org = org_cache[depth];
                    StepDDAContext(ctx);
#endif

#ifdef FIXED_CONTEXT
                    alt_ctx = FixedPointDDARestore(alt_cache[depth], dir, LOG_CHUNK_SIZE);
                    FixedPointDDAStep(alt_ctx);
#endif

                    CHECK_INTERRUPT;
                }
                else
                    break;
            }
            else if (depth > stack_depth)
            {

#ifdef INLINE_RESULTS
                float level_size = 1.0;
                for (uint index = 0; index < stack_depth; index++)
                {
                    float voxel_count = ((index & 1) != 0 ? CHUNK_SIZE : node_size);

#ifdef FIXED_CONTEXT
                    t += FixedPointDDABackupHitT(alt_cache[index], alt_ctx.direction) * level_size;
#else
                    t += HitTDDAContextSave(ctx_cache[index], dir, voxel_count) * level_size;
#endif

                    level_size /= voxel_count;
                }
#ifdef FIXED_CONTEXT
                t += FixedPointDDAHitT(alt_ctx) * level_size;
#else
                t += HitTDDAContext(ctx) * level_size;
#endif
#endif

                break;
            }
        }
        else
        {
            while (
#ifdef FIXED_CONTEXT
                alt_ctx.mask.w == 1.0
#else
                ctx.mask.w == 1.0
#endif
            )
            {
                ITERATION_CONTROL;

#ifdef FIXED_CONTEXT
                ivec3 int_coords = FixedPointDDACurrentVoxel(alt_ctx);
#else
                uvec3 int_coords = VoxelCoordsDDAContext(ctx);
#endif

                uint side_index = int_coords.z / 2;
                uint voxel_index = int_coords.x + int_coords.y * 4 + (int_coords.z%2)*16;

                if ((data[depth-1][side_index] & (1<<voxel_index)) != 0)
                {
                    hit = true;
                    if (depth < stack_depth)
                    {

#ifdef FIXED_CONTEXT
                        uint base_offset =
                            texture(offsets[nonuniformEXT(current_tex)],
                                    FixedPointDDABackupTextureCoords(alt_cache[depth-1])).x;
#else
                        uint base_offset =
                            texture(offsets[nonuniformEXT(current_tex)],
                                    TextureCoordsDDAContextSave(ctx_cache[depth-1])).x;
#endif

                        if (base_offset != 0xffffffff)
                        {
                            tex_index[depth] = current_tex;

                            uint offset =
                                bitCount(data[depth-1][side_index] & ((1<<voxel_index)-1));

                            if (side_index == 1)
                                offset += bitCount(data[depth-1][0]);

                            current_tex = offset + base_offset;
                        }

                        if (base_offset == 0xffffffff
                            || (0 == ((residency_mask[current_tex >> 5] >> (current_tex & 0x1fu)) & 1u)))
                        {
#ifdef INLINE_RESULTS
#ifdef FIXED_CONTEXT
                            output_value = FixedPointDDAHitNormal(alt_ctx);
#else
                            output_value = HitNormalDDAContext(ctx);
#endif
                            payload = vec3(1.0);
#endif

                            depth_bypass = depth;
                            depth = stack_depth+1;
                            break;
                        }

#ifdef FLOAT_CONTEXT
                        ctx_cache[depth] = SaveDDAContext(ctx);
                        org_cache[depth] = org;
                        org = RecursionPointDDAContext(ctx, org, dir, CHUNK_SIZE);
                        ctx = InitDDAContext(org, dir, node_size);
                        ctx.mask.xyz = ctx_cache[depth].mask.xyz;
#endif

#ifdef FIXED_CONTEXT
                        alt_cache[depth] = FixedPointDDASave(alt_ctx);
                        alt_ctx = FixedPointDDARecursion(alt_ctx, dir, log_node_size);
#endif

                        ++depth;
                    }
                    else
                    {

#ifdef INLINE_RESULTS
#ifdef FIXED_CONTEXT
                        output_value = FixedPointDDAHitNormal(alt_ctx);
#else
                        output_value = HitNormalDDAContext(ctx);
#endif

#if 0
                        vec3 chunk_coords =
                            (vec3(int_coords) + vec3(0.5)) / (CHUNK_SIZE * node_size);
                        vec3 texture_coords = TextureCoordsDDAContextSave(ctx_cache[depth-1]);

                        payload = texture(payloads[nonuniformEXT(current_tex - leaves_begin)],
                                          texture_coords - vec3(0.5)/node_size + chunk_coords).xyz;
#endif

                        payload = vec3(1.0);
#endif

                        ++depth;
                    }
                    break;
                }

#ifdef FLOAT_CONTEXT
                StepDDAContext(ctx);
#endif

#ifdef FIXED_CONTEXT
                FixedPointDDAStep(alt_ctx);
#endif

                CHECK_INTERRUPT;
            }

            if (!hit)
            {
                if (depth > 0)
                {
                    --depth;
                    current_tex = tex_index[depth];

#ifdef FLOAT_CONTEXT
                    ctx = RestoreDDAContext(ctx_cache[depth], dir, node_size);
                    org = org_cache[depth];
                    StepDDAContext(ctx);
#endif

#ifdef FIXED_CONTEXT
                    alt_ctx = FixedPointDDARestore(alt_cache[depth], dir, log_node_size);
                    FixedPointDDAStep(alt_ctx);
#endif

                    CHECK_INTERRUPT;
                }
                else
                    break;
            }
            else if (depth > stack_depth)
            {

#ifdef INLINE_RESULTS
                float level_size = 1.0;
                for (uint index = 0; index < min(depth_bypass, stack_depth); index++)
                {
                    float voxel_count = ((index & 1) != 0 ? CHUNK_SIZE : node_size);

#ifdef FIXED_CONTEXT
                    t += FixedPointDDABackupHitT(alt_cache[index], alt_ctx.direction) * level_size;
#else
                    t += HitTDDAContextSave(ctx_cache[index], dir, voxel_count) * level_size;
#endif

                    level_size /= voxel_count;
                }

#ifdef FIXED_CONTEXT
                t += FixedPointDDAHitT(alt_ctx) * level_size;
#else
                t += HitTDDAContext(ctx) * level_size;
#endif
#endif

                break;
            }
        }
    }

    if (hit)
    {
#ifndef INLINE_RESULTS
        float t = 0.0;
        vec3 output_value = vec3(0.0);

        float level_size = 1.0;
        for (uint index = 0; index < min(depth_bypass, stack_depth); index++)
        {
            float voxel_count = ((index & 1) != 0 ? CHUNK_SIZE : node_size);

#ifdef FIXED_CONTEXT
            t += FixedPointDDABackupHitT(alt_cache[index], alt_ctx.direction) * level_size;
#else
            t += HitTDDAContextSave(ctx_cache[index], dir, voxel_count) * level_size;
#endif

            level_size /= voxel_count;
        }

#ifdef FIXED_CONTEXT
        t += FixedPointDDAHitT(alt_ctx) * level_size;
        output_value = FixedPointDDAHitNormal(alt_ctx);
#else
        t += HitTDDAContext(ctx) * level_size;
        output_value = HitNormalDDAContext(ctx);
#endif

#if 0
        vec3 chunk_coords =
            (vec3(int_coords) + vec3(0.5)) / (CHUNK_SIZE * node_size);
        vec3 texture_coords = TextureCoordsDDAContextSave(ctx_cache[depth-1]);

        payload = texture(payloads[nonuniformEXT(current_tex - leaves_begin)],
                          texture_coords - vec3(0.5)/node_size + chunk_coords).xyz;
#endif

        payload = vec3(1.0);
#endif

        return vec4(output_value, t);
    }
    else
    {
#ifdef FIXED_CONTEXT
        return vec4(vec3(0.0), -1.0);
        //return vec4((FixedPointDDAHitNormal(alt_ctx) * 0.5 + vec3(0.5)) * 0.1, -1.0);
#else
        return vec4(vec3(0.0), -1.0);
        //return vec4((HitNormalDDAContext(ctx) * 0.5 + vec3(0.5)) * 0.1, -1.0);
#endif
    }
#endif
}

vec3 CircleIntersection(float radius, float org_height, float ray_polar)
{
    float b = org_height * ray_polar;
    float det = b*b - (org_height*org_height - radius*radius);
    float sqrt_det = sqrt(det);
    vec3 result = vec3(det, -b - sqrt_det, -b + sqrt_det);
    if (result.x > 0.0 && result.y < 0.0 && result.z < 0.0)
        result.x = -result.x;
    return result;
}

#define STEP_COUNT 128.0
#define STEP_COUNT_SECONDARY 8.0
vec3 ClearSky_Inscattering_Ray(vec3 position, vec3 direction)
{
    const float light_angle = dot(direction, light_dir);

    vec3 origin = vec3(0.0, base_radius + 0.000001, 0.0) + position.xyz / scale_factor;
    float altitude = origin.y;

    vec3 primary_ground_hit =
        CircleIntersection(base_radius, altitude, direction.y);
    if (primary_ground_hit.x > 0.0)
        //return vec3(0.1);
        return vec3(0.0);

    vec3 primary_atmosphere_hit =
        CircleIntersection(outer_radius, altitude, direction.y);
    if (primary_atmosphere_hit.x <= 0.0)
        return direction * 0.5 + vec3(0.5);

    float step_size = primary_atmosphere_hit.z / STEP_COUNT;

    vec3 optical_depths = vec3(0.0);
    vec3 inscattering = vec3(0.0);

    for (float index_primary = 0.0; index_primary < STEP_COUNT; ++index_primary)
    {
        vec3 step = origin + step_size * (index_primary + 0.5) * direction;
        float sample_height = length(step) - base_radius;

        vec3 densities = vec3(
            RayleighDensity(sample_height),
            MieDensity(sample_height),
            OzoneDensity(sample_height)
        );
        vec3 optical_step = densities * step_size;

        vec3 light_optical_depths = vec3(0.0);

        vec3 secondary_atmosphere_hit =
            CircleIntersection(outer_radius, sample_height, dot(normalize(step), light_dir));

        vec3 secondary_ground_hit =
            CircleIntersection(base_radius, sample_height, dot(normalize(step), light_dir));
        if (secondary_ground_hit.x <= 0.0)
            continue;

        float light_step_size = secondary_atmosphere_hit.z / STEP_COUNT_SECONDARY;
        for (float index_light = 0.0; index_light < STEP_COUNT_SECONDARY; ++index_light)
        {
            vec3 light_step = step + (index_light + 0.5) * light_step_size * light_dir;
            float light_height = length(light_step) - base_radius;

            vec3 light_densities = vec3(
                RayleighDensity(light_height),
                MieDensity(light_height),
                OzoneDensity(light_height)
            );

            light_optical_depths += light_densities * light_step_size;
        }

        vec3 current_depths = optical_depths + light_optical_depths;
        vec3 transmittance = exp(
            -rayleigh_scattering * current_depths.x
            -(mie_scattering+mie_absorption).xxx * current_depths.y
            -ozone_absorption * current_depths.z);

        vec3 rayleigh_transmittance = densities.x * transmittance;
        vec3 mie_transmittance = densities.y * transmittance;

        inscattering += sun_intensity * step_size *
            (rayleigh_scattering * RayleighPhase(light_angle) * rayleigh_transmittance
             + mie_scattering * MiePhase(light_angle) * mie_transmittance);

        optical_depths += optical_step;
    }

    return inscattering;
}

vec3 ClearSky_Direct_Ray(vec3 position)
{
    vec3 origin = vec3(0.0, base_radius + 0.000001, 0.0) + position.xyz / scale_factor;
    float altitude = origin.y;
    vec3 direction = light_dir;

    vec3 primary_ground_hit =
        CircleIntersection(base_radius, altitude, direction.y);
    if (primary_ground_hit.x > 0.0)
        return vec3(0.0);

    vec3 primary_atmosphere_hit =
        CircleIntersection(outer_radius, altitude, direction.y);
    if (primary_atmosphere_hit.x <= 0.0)
        return direction * 0.5 + vec3(0.5);

    float step_size = primary_atmosphere_hit.z / STEP_COUNT;

    vec3 optical_depths = vec3(0.0);

    for (float index_primary = 0.0; index_primary < STEP_COUNT; ++index_primary)
    {
        vec3 step = origin + step_size * (index_primary+0.5) * direction;
        float sample_height = length(step) - base_radius;

        vec3 densities = vec3(
            RayleighDensity(sample_height),
            MieDensity(sample_height),
            OzoneDensity(sample_height)
        );
        vec3 optical_step = densities * step_size;
        optical_depths += optical_step;
    }

    vec3 transmittance = exp(
        -rayleigh_scattering * optical_depths.x
        -(mie_scattering+mie_absorption).xxx * optical_depths.y
        -ozone_absorption * optical_depths.z);

    return transmittance * sun_intensity;
}

vec2 WorldToSkybox(vec3 dir)
{
    float theta = asin(dir.y);
    float major_axis = dot(dir.xz, vec2(cos(light_phi), sin(light_phi)));
    float phi = acos(clamp(major_axis / cos(theta), -1.0, 1.0));

    vec2 uv = vec2(phi, theta) / (3.14159265 * vec2(1.0, 0.5));
    uv.y = sign(uv.y) * sqrt(abs(uv.y)) * 0.5 + 0.5;

    return uv;
}

vec3 ClearSky_Inscattering_LUT(vec3, vec3 direction)
{
    return texture(skybox, WorldToSkybox(direction)).xyz;
}

vec3 ClearSky_Direct_LUT(vec3 position)
{
    float altitude = position.y / scale_factor;
    vec3 tr = TransmittanceLookup(altitude, light_dir.y, trlut);
    return tr * sun_intensity;
}

vec3 ClearSky_Irradiance(vec3 position, vec3 direction)
{
    vec3 irradiance = vec3(0.0);

    direction = normalize(direction);
    if (dot(direction, light_dir) > cos(0.015)
        && direction.y > 0.0)
    {
        if (precomputed_sky != 0)
            irradiance += ClearSky_Direct_LUT(position);
        else
            irradiance += ClearSky_Direct_Ray(position);
    }

    if (precomputed_sky != 0)
        irradiance += ClearSky_Inscattering_LUT(position, direction);
    else
        irradiance += ClearSky_Inscattering_Ray(position, direction);


    return irradiance;
}

#define SAMPLE_COUNT 4
vec3 GenerateRay(uint pixelID, uint pixel_count, uint sample_index, vec3 normal)
{
    vec2 owen_sample = OwenPlastic(
        pixel_count * SAMPLE_COUNT * sample_offset +
        pixelID * SAMPLE_COUNT + sample_index, 0);

    float sin_theta = sqrt(1.0 - owen_sample.x*owen_sample.x);
    float phi = owen_sample.y * 3.14159265 * 2.0;
    vec3 ts_sample = vec3(
        cos(phi) * sin_theta,
        sin(phi) * sin_theta,
        owen_sample.x
    );

    mat3 tb_mat = genTB(normal);
    vec3 current_ray = normalize(tb_mat * ts_sample);

    return current_ray;
}

void main()
{
    vec3 box_scale = region_scale;
    vec3 local_camera = (iFS_camera_position * 0.5 + vec3(0.5)) * box_scale;
    vec3 local_coords = iFS_volume_coords * box_scale;

    vec2 uv =
        ((vec2(gl_FragCoord.xy) - vec2(screen_offset.xy) + pixelPlasticSequence(sample_offset))
        / vec2(screen_size.xy));
    uint pixelID = morton2D(uvec2(gl_FragCoord.xy));
    uint pixelCount = uint(screen_size.x) * uint(screen_size.y);

    vec3 dir =
        (inverse(mat3(view_model)) *
         inverse(mat3(projection))
         * vec3(uv * 2.0 - vec2(1.0), 1.0));
    dir = normalize(dir*box_scale);

    vec3 step_sign = sign(-dir);
    vec3 max_t = abs(((step_sign * 0.5 + vec3(0.5)) * box_scale - (step_sign * local_coords)) / dir);

    vec3 root_org = local_camera;
    if (root_org != clamp(root_org, vec3(0.0), vec3(1.0) * box_scale))
    {
        vec3 alt_org = root_org - vec3(0.5) * box_scale;
        vec3 abs_org = abs(alt_org) * (2.0 / box_scale);
        float winding = max(abs_org.x, max(abs_org.y, abs_org.z)) < 1.0 ? -1.0 : 1.0;
        vec3 sign_bit = -sign(dir);
        vec3 d = (0.5 * box_scale * winding * sign_bit - alt_org) / dir;

#define TEST(U, VW) (d.U > 0.0) && all(lessThan(abs(alt_org.VW + dir.VW * d.U), vec2(0.5 * box_scale.VW)))

        bvec3 test = bvec3(TEST(x, yz), TEST(y, zx), TEST(z, xy));
        sign_bit = test.x
            ? vec3(sign_bit.x, 0.0, 0.0)
            : (test.y
                ? vec3(0.0, sign_bit.y, 0.0)
                : vec3(0.0, 0.0, test.z ? sign_bit.z : 0.0));
#undef TEST

        if (test == bvec3(false))
            discard;

        float distance = (sign_bit.x != 0.0) ? d.x : ((sign_bit.y != 0.0) ? d.y : d.z);
        vec3 temp = local_camera + dir * distance;
        root_org = mix(local_camera + dir * distance,
                       (box_scale * (-sign(dir) * 0.5 + 0.5)),
                       abs(sign_bit));
    }

    float entry_depth = distance(local_camera, root_org);
    float region_begin = min(max_t.x, min(max_t.y, max_t.z));

    vec3 base_color = vec3(0.0);
    vec3 org = root_org;

#if 1
    TraversalContext main_ctx;

    ivec3 forg = FixedPointDDARayOrigin(org);
    ivec3 fdir = FixedPointDDARayDirection(dir);
    ivec3 light_fdir = FixedPointDDARayDirection(light_dir);
    //bool hit = RegionTraversal_Fixed(forg, fdir);
    vec2 hit_uv;
    bool hit = RegionTraversal_Fixed(main_ctx, forg, fdir, hit_uv);

    vec3 indirect = vec3(0.0);
    vec3 exit_color = vec3(0.0);
    vec3 current_weight = vec3(1.0);
    float depth = 1.0;

    if (hit)
    {
        vec3 base_color = vec3(hit_uv.xy, 0.0);//vec3(1.0);
        vec2 threshold = step(vec2(0.0), abs(hit_uv.xy * 2.0 - 1.0) - 0.95);
        base_color = vec3(1.0 - min(dot(threshold, threshold), 0.9));

#if 0
        vec3 hitn = vec3(fdir);
        vec3 hitp = FixedToFloat(forg);
        fdir = light_fdir;
        float shadow = RegionTraversal_Fixed(forg, fdir) ? 0.0 : 1.0;
        vec3 diffuse = max(0.0, dot(light_dir, hitn)) * ClearSky_Irradiance(hitp, light_dir);
        color.xyz = base_color * (shadow.xxx * diffuse);
#endif

        vec3 previous_hitp = FixedToFloat(forg);
        vec3 previous_hitn = vec3(fdir);
        depth = length(previous_hitp - org);

        for (uint sample_index = 0; sample_index < SAMPLE_COUNT; ++sample_index)
        {
            if (dot(previous_hitn, light_dir) > 0.0)
            {
                TraversalContext light_ctx;
                ivec3 direct_org = forg;
                ivec3 direct_dir = light_fdir;
                //if (!RegionTraversal_Fixed(direct_org, direct_dir))
                //if (!RegionTraversal_Fixed(light_ctx, direct_org, direct_dir))
                if (!RegionTraversal_Query(light_ctx, forg, light_fdir))
                    indirect += max(0.0, dot(previous_hitn, light_dir))
                        * current_weight
                        * ClearSky_Irradiance(previous_hitp, light_dir);
            }

            vec3 current_ray = GenerateRay(pixelID, pixelCount, sample_index, previous_hitn);

            fdir = FixedPointDDARayDirection(current_ray);

            float next_weight = dot(previous_hitn, current_ray);
            //if (!RegionTraversal_Fixed(forg, fdir))
            if (!RegionTraversal_Fixed(main_ctx, forg, fdir))
            {
                exit_color =
                    ClearSky_Irradiance(previous_hitp, current_ray)
                    * current_weight
                    * next_weight;
                break;
            }

            current_weight *= next_weight;
            previous_hitp = FixedToFloat(forg);
            previous_hitn = vec3(fdir);
        }

        color.xyz = base_color * (indirect + exit_color);
    }
    else
    {
        color.xyz = ClearSky_Irradiance(org, dir);
    }

    gl_FragDepth = depth + entry_depth;

#endif

#if 0
    vec4 trav = RegionTraversal4(org, dir, base_color);
    vec3 payload = base_color;

#if 0
    color.xyz = payload - trav.xyz;
    color.w = 1.0;
    return;
#endif

    if (trav.w >= 0.0)
    {
        float frag_depth = trav.w + entry_depth;

#if 0
        color.xyz = frag_depth.xxx;
        //color.xyz = trav.xyz * 0.5 + vec3(0.5);
        color.w = 1.0;
        return;

#else
        float fog = exp(-frag_depth * 10.0);

        vec3 hitp = org + dir*trav.w;
        float shadow = 0.0;
        vec3 diffuse = vec3(0.0);

        vec3 indirect = vec3(0.0);
        vec3 exit_color = vec3(0.0);
        vec3 avg_dir = vec3(0.0);
        float avg_t = 0.0;
        if (display_mode == 0.0)
        {
            shadow = RegionTraversal4(hitp + light_dir*0.0001, light_dir, payload).w >= 0.0 ? 0.0 : 1.0;
            diffuse = max(0.0, dot(light_dir, trav.xyz)) *
                ClearSky_Irradiance(hitp, light_dir);
        }
        else
        {
            vec4 previous_hit = trav;
            vec3 previous_org = org;
            vec3 previous_dir = dir;
            vec3 current_weight = vec3(1.0) * payload;

#if 1
            for (uint sample_index = 0; sample_index < SAMPLE_COUNT; ++sample_index)
            {
                vec3 previous_hitp = previous_org + previous_dir*previous_hit.w;
                vec3 current_org = previous_hitp + previous_hit.xyz*0.0001;

                if (dot(previous_hit.xyz, light_dir) > 0.0)
                {
                    vec3 dummy = vec3(0.0);
                    float sample_t = RegionTraversal4(current_org + light_dir*0.0001, light_dir, dummy).w;
                    if (sample_t < 0.0)
                    {
                        indirect += max(0.0, dot(previous_hit.xyz, light_dir))
                            * current_weight
                            * ClearSky_Irradiance(current_org, light_dir);
                    }
                }

                vec2 owen_sample = OwenPlastic(
                    pixelCount * SAMPLE_COUNT * sample_offset +
                    pixelID * SAMPLE_COUNT + sample_index, 0);

                float sin_theta = sqrt(1.0 - owen_sample.x*owen_sample.x);
                float phi = owen_sample.y * 3.14159265 * 2.0;
                vec3 ts_sample = vec3(
                    cos(phi) * sin_theta,
                    sin(phi) * sin_theta,
                    owen_sample.x
                );

                mat3 tb_mat = genTB(previous_hit.xyz);
                vec3 current_ray = normalize(tb_mat * ts_sample);

                vec3 next_payload = vec3(0.0);
                vec4 current_hit = RegionTraversal4(current_org, current_ray, next_payload);
                // equivalent to dot(previous_hit.xyz, current_ray)
                float next_weight = ts_sample.z; // dot(previous_hit.xyz, current_ray)
                if (current_hit.w < 0.0)
                {
                    exit_color =
                        ClearSky_Irradiance(current_org, current_ray)
                        * current_weight
                        * next_weight;
                    break;
                }

                payload = next_payload;
                current_weight *= next_weight * payload;
                previous_org = current_org;
                previous_dir = current_ray;
                previous_hit = current_hit;
            }

#else
#endif

        }

        //color.xyz = vec3(mix(shadow.xxx * diffuse + indirect.xxx * indirect_contribution, vec3(0.3, 0.5, 0.5), (1.0 - fog).xxx));
        if (display_mode < 2.0)
            color.xyz = base_color * (shadow.xxx * diffuse) + indirect + exit_color;
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
        color.xyz = ClearSky_Irradiance(org, dir);
#endif


    color.w = 1.0;
}
