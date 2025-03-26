R"_(

struct DDAContext
{
    vec3 step_sign;
    vec3 exit_value;
    vec3 delta_t;
    vec3 max_t;
    vec3 voxel_coords;
    vec3 voxel_step;
    vec4 mask;
    float t;
};

#define EPSILON 1.0e-5

DDAContext InitDDAContext(vec3 org, vec3 dir, float voxel_count)
{
    vec3 voxel_space_org = clamp(org, vec3(0.0), vec3(1.0-EPSILON)) * voxel_count;

    DDAContext ctx;

    ctx.step_sign = sign(dir);
    ctx.exit_value = ctx.step_sign * 0.5 + vec3(0.5);
    vec3 edge_distance = ctx.exit_value - (ctx.step_sign * fract(voxel_space_org));
    ctx.delta_t = 1.0 / (abs(dir) * voxel_count);
    ctx.max_t = abs(edge_distance) * ctx.delta_t;
    ctx.voxel_coords = (floor(voxel_space_org) + vec3(0.5)) / voxel_count;
    ctx.voxel_step = ctx.step_sign / voxel_count;

    ctx.mask.xyz = vec3(0.0);
    // Equivalent to mask =
    // ((voxel_coords.x * step_sign.x < exit_value.x) &&
    // (voxel_coords.y * step_sign.y < exit_value.y) &&
    // (voxel_coords.z * step_sign.z < exit_value.z)) ? 1.0 : 0.0
    ctx.mask.w =
        (1.0 - step(ctx.exit_value.x, ctx.voxel_coords.x * ctx.step_sign.x))
        * (1.0 - step(ctx.exit_value.y, ctx.voxel_coords.y * ctx.step_sign.y))
        * (1.0 - step(ctx.exit_value.z, ctx.voxel_coords.z * ctx.step_sign.z));

    ctx.t = 0.0;

    return ctx;
}

void StepDDAContext(inout DDAContext ctx)
{
    vec3 order = vec3(
        // ctx.max_t.x < ctx.max_t.y
        1.0 - step(ctx.max_t.y, ctx.max_t.x),
        // ctx.max_t.x < ctx.max_t.z
        1.0 - step(ctx.max_t.z, ctx.max_t.x),
        // ctx.max_t.y < ctx.max_t.z
        1.0 - step(ctx.max_t.z, ctx.max_t.y));

    ctx.mask.xyz = vec3(
        // X && Y
        order.x * order.y,
        // !X && Z
        (1.0 - order.x) * order.z,
        // (X && !Y) || (!X && Z)
        order.x * (1.0 - order.y) + (1.0 - order.x) * (1.0 - order.z)
        );

    ctx.t = min(ctx.max_t.x, min(ctx.max_t.y, ctx.max_t.z));
    ctx.voxel_coords += ctx.voxel_step * ctx.mask.xyz * ctx.mask.w;
    ctx.max_t += ctx.delta_t * ctx.mask.xyz * ctx.mask.w;

    ctx.mask.w =
        abs(sign(dot(ctx.voxel_step, ctx.mask.xyz))) *
        (1.0 - step(ctx.exit_value.x, ctx.voxel_coords.x * ctx.step_sign.x))
        * (1.0 - step(ctx.exit_value.y, ctx.voxel_coords.y * ctx.step_sign.y))
        * (1.0 - step(ctx.exit_value.z, ctx.voxel_coords.z * ctx.step_sign.z));
}

vec3 RecursionPointDDAContext(DDAContext ctx, vec3 org, vec3 dir, float region_size)
{
    return (org+dir*ctx.t) * region_size * (1.0 - EPSILON*ctx.step_sign)
        - floor(ctx.voxel_coords * region_size);
}

vec3 HitNormalDDAContext(DDAContext ctx)
{
    return ctx.mask.xyz * -ctx.step_sign;
}

struct DDAContextSave
{
    vec4 mask;
    vec3 voxel_coords;
    vec3 max_t;
};

float DDAContextSaveT(in DDAContextSave ctx, vec3 dir, float voxel_count)
{
    vec3 delta_t = 1.0 / (abs(dir) * voxel_count);
    vec3 max_t = ctx.max_t - delta_t * ctx.mask.xyz * ctx.mask.w;
    return min(max_t.x, min(max_t.y, max_t.z));

    //return min(ctx.max_t.x, min(ctx.max_t.y, ctx.max_t.z));
}

DDAContextSave SaveDDAContext(inout DDAContext ctx)
{
    DDAContextSave save;
    save.mask = ctx.mask;
    save.voxel_coords = ctx.voxel_coords;
    save.max_t = ctx.max_t;
    return save;
}

DDAContext RestoreDDAContext(inout DDAContextSave save, vec3 dir, float voxel_count)
{
    DDAContext ctx;

    ctx.step_sign = sign(dir);
    ctx.exit_value = ctx.step_sign * 0.5 + vec3(0.5);
    ctx.delta_t = 1.0 / (abs(dir) * voxel_count);
    ctx.voxel_step = ctx.step_sign / voxel_count;

    ctx.max_t = save.max_t;
    ctx.voxel_coords = save.voxel_coords;
    ctx.mask = save.mask;
    ctx.t = min(ctx.max_t.x, min(ctx.max_t.y, ctx.max_t.z));

    return ctx;
}

)_"
