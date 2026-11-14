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

#define FIXED_PRECISION 1073741824
#define FIXED_BIT_COUNT 30
#define FIXED_LOW_MASK 0x7fff
#define FIXED_LOW_OFFSET 15

ivec3 FloatToFixed(vec3 value)
{
    return ivec3(value * vec3(1073741824.0));
}

vec3 FixedToFloat(ivec3 value)
{
    return vec3(value) / vec3(1073741824.0);
}

ivec3 FixedProduct(ivec3 a, ivec3 b)
{
    uvec3 al = uvec3(a & ivec3(0x7fff));
    ivec3 ah = a >> ivec3(15);
    uvec3 bl = uvec3(b & ivec3(0x7fff));
    ivec3 bh = b >> ivec3(15);

    uvec3 low = al*bl;
    ivec3 mid_a = ivec3(al)*bh + ivec3(low >> 15);
    ivec3 mid_low = mid_a & ivec3(0x7fff);
    ivec3 mid_high = mid_a >> ivec3(15);
    mid_low = ivec3(bl)*ah + mid_low;
    ivec3 high = ah*bh;

    return high + (mid_low >> 15) + mid_high;
}

ivec3 FixedQuotient(ivec3 a, ivec3 b)
{
    return (a / (b >> 15)) << 15;
}

struct FixedPointDDA {
    ivec3 current_position;
    //ivec3 current_voxel;
    //ivec3 step_sign;
    ivec3 direction;
    ivec3 delta_t;
    ivec3 max_t;
    int log_size;
    ivec4 mask;

    ivec3 forg;

#if 0
    vec3 dir;
    ivec3 fdir;
#endif
};

struct FixedPointDDABackup {
    ivec3 current_position;
    //ivec3 current_voxel;
    ivec3 max_t;
    ivec4 mask;

    ivec3 forg;
};

ivec3 FixedPointDDACurrentVoxel(FixedPointDDA dda)
{
    return dda.current_position >> (FIXED_BIT_COUNT - dda.log_size);
}

int FixedPointDDAPrecision(FixedPointDDA dda)
{
    return FIXED_PRECISION >> dda.log_size;
}

vec3 FixedPointDDATextureCoords(FixedPointDDA dda)
{
    return (vec3(FixedPointDDACurrentVoxel(dda)) + 0.5) / float(1 << log_size);
}

vec3 FixedPointDDAHitNormal(FixedPointDDA dda)
{
    ivec3 voxel_size = FixedPointDDAPrecision(dda);
    ivec3 current_voxel = FixedPointDDACurrentVoxel(dda);
    ivec3 direction = dda.current_position - (current_voxel * voxel_size + voxel_size / 2);

    ivec3 order = ivec3(
        1 - step(direction.x, direction.y),
        1 - step(direction.x, direction.z),
        1 - step(direction.y, direction.z));

    ivec3 normal = ivec3(
        order.x * order.y,
        (1 - order.x) * order.z,
        order.x * (1 - order.y) + (1 - order.x) * (1 - order.z)
    );

    return vec3(-normal * sign(dda.fdir));
}

float FixedPointDDAHitT(FixedPointDDA dda)
{
    vec3 ray = FixedToFloat(dda.current_position - dda.forg);
    return length(ray);
}

FixedPointDDA FixedPointDDAInit_Base(ivec3 forg, vec3 dir, int log_voxel_count)
{
    //org = clamp(org, vec3(0.0), vec3(1.0-EPSILON));

    //ivec3 forg = clamp(FloatToFixed(org), 0, (FIXED_PRECISION-1));

    FixedPointDDA ctx;
#if 0
    ctx.dir = dir;
    ctx.fdir = FloatToFixed(dir);
#endif
    ctx.forg = forg;

    ctx.current_position = forg;
    //ctx.current_voxel = forg >> (FIXED_BIT_COUNT - log_voxel_count);
    //ctx.step_sign = ivec3(sign(dir));
    ctx.direction = FloatToFixed(dir);
    ivec3 step_sign = sign(ctx.direction);

    //ctx.size = 1 << log_voxel_count;
    ctx.log_size = log_voxel_count;

    {
        int voxel_precision = FIXED_PRECISION >> ctx.log_size;
        int voxel_mask = voxel_precision - 1;
        ivec3 forg_subvoxel = forg & voxel_mask;

        ivec3 exit_value = ((voxel_precision * step_sign) >> 1) + (voxel_precision >> 1);
        ivec3 edge_distance = (exit_value - (step_sign * forg_subvoxel)) << ctx.log_size;
        float voxel_count = float(1 << ctx.log_size);

        ctx.delta_t = FloatToFixed(min(1.0 / (abs(dir) * voxel_count), vec3(1.0)));
        ctx.max_t = FixedProduct(edge_distance, ctx.delta_t);
    }

    ivec3 order = ivec3(
        1 - step(ctx.max_t.y, ctx.max_t.x),
        1 - step(ctx.max_t.z, ctx.max_t.x),
        1 - step(ctx.max_t.z, ctx.max_t.y));

    ctx.mask.xyz = ivec3(
        order.x * order.y,
        (1 - order.x) * order.z,
        order.x * (1 - order.y) + (1 - order.x) * (1 - order.z)
    );

    int size = (1 << ctx.log_size) - 1;
    ivec3 exit_value = ((size.xxx * step_sign) + size.xxx) >> 1;
    ivec3 current_voxel = FixedPointDDACurrentVoxel(ctx);
    ctx.mask.w =
        int((current_voxel.x * step_sign.x <= exit_value.x)
            && (current_voxel.y * step_sign.y <= exit_value.y)
            && (current_voxel.z * step_sign.z <= exit_value.z));

    return ctx;
}

void FixedPointDDAStep(inout FixedPointDDA ctx)
{
    ivec3 order = ivec3(
        1 - step(ctx.max_t.y, ctx.max_t.x),
        1 - step(ctx.max_t.z, ctx.max_t.x),
        1 - step(ctx.max_t.z, ctx.max_t.y));

    ctx.mask.xyz = ivec3(
        order.x * order.y,
        (1 - order.x) * order.z,
        order.x * (1 - order.y) + (1 - order.x) * (1 - order.z)
    );

    ivec3 step_sign = sign(ctx.direction);
    //ctx.current_voxel += ctx.mask.xyz * step_sign;
    ivec3 min_t = ctx.mask.xyz * ctx.max_t;
    int step_factor = min_t.x + min_t.y + min_t.z;
    ivec3 step = ctx.mask.xyz * ctx.delta_t;
    ctx.max_t += step - step_factor;

    ivec3 offset = FixedProduct(step_factor.xxx, ctx.direction) * (1 - ctx.mask.xyz);
    int voxel_mask = (FIXED_PRECISION >> ctx.log_size) - 1;

    ivec3 position_check = (ctx.current_position + offset) & voxel_mask;

    ivec3 position_mask = ~(voxel_mask * ctx.mask.xyz);
    ivec3 position_intermediate = ctx.current_position + offset;
    ivec3 current_position = ctx.current_position;
    ivec3 next_position =
        ((ctx.current_position + offset) & position_mask)
        // manually align along the direction of movement
        + ctx.mask.xyz * (max(step_sign, ivec3(0)) * voxel_mask + step_sign);
    ctx.current_position = next_position;

    ivec3 current_voxel = FixedPointDDACurrentVoxel(ctx);

    {
        int voxel_mask = (FIXED_PRECISION >> ctx.log_size) - 1;
        ivec3 alignment_check = next_position & voxel_mask;
        ivec3 voxel_check = next_position >> (FIXED_BIT_COUNT - ctx.log_size);
        ivec3 previous_voxel = current_position >> (FIXED_BIT_COUNT - ctx.log_size);
        int i = 0;
    }

    int size = (1 << ctx.log_size) - 1;
    ivec3 exit_value = ((size.xxx * step_sign) + size.xxx) >> 1;
    ctx.mask.w =
        int((current_voxel.x * step_sign.x <= exit_value.x)
            && (current_voxel.y * step_sign.y <= exit_value.y)
            && (current_voxel.z * step_sign.z <= exit_value.z));
}

FixedPointDDABackup FixedPointDDASave(inout FixedPointDDA ctx)
{
    FixedPointDDABackup save;
    save.current_position = ctx.current_position;
    //save.current_voxel = ctx.current_voxel;
    save.mask = ctx.mask;
    save.max_t = ctx.max_t;

    save.forg = ctx.forg;
    return save;
}

FixedPointDDA FixedPointDDARestore(inout FixedPointDDABackup save, vec3 dir, int log_voxel_count)
{
    FixedPointDDA ctx;
    ctx.current_position = save.current_position;
    //ctx.current_voxel = save.current_voxel;
    //ctx.step_sign = ivec3(sign(dir));
    ctx.direction = FloatToFixed(dir);

    float voxel_count = float(1 << log_voxel_count);
    ctx.delta_t = FloatToFixed(min(1.0 / (abs(dir) * voxel_count), vec3(1.0)));

    ctx.max_t = save.max_t;
    //ctx.size = 1 << log_voxel_count;
    ctx.log_size = log_voxel_count;
    ctx.mask = save.mask;

    ctx.forg = save.forg;

    return ctx;
}

FixedPointDDA FixedPointDDAInit(vec3 org, vec3 dir, int log_voxel_count)
{
    ivec3 forg = clamp(FloatToFixed(org), 0, (FIXED_PRECISION-1));
    return FixedPointDDAInit_Base(forg, dir, log_voxel_count);
}

FixedPointDDA FixedPointDDARecursion(inout FixedPointDDA parent, vec3 dir, int log_voxel_count)
{
    ivec3 parent_subvoxel = parent.current_position & (FixedPointDDAPrecision(parent) - 1);
    ivec3 forg = parent_subvoxel << parent.log_size;

    return FixedPointDDAInit_Base(forg, dir, log_voxel_count);
}

vec3 InitDeltaT(vec3 dir, float voxel_count)
{
    // bound 1.0/0.0 with largest representable value, we only need to avoid
    // inf eventually turning into nan through 0 * inf
    return min(1.0 / (abs(dir) * voxel_count), vec3(3.4028235e38));
}

// Amanatides & Woo 87
DDAContext InitDDAContext(vec3 org, vec3 dir, float voxel_count)
{
    vec3 voxel_space_org = clamp(org, vec3(0.0), vec3(1.0-EPSILON)) * voxel_count;

    DDAContext ctx;

    ctx.step_sign = sign(dir);
    ctx.exit_value = ctx.step_sign * 0.5 + vec3(0.5);
    vec3 edge_distance = ctx.exit_value - (ctx.step_sign * fract(voxel_space_org));
    ctx.delta_t = InitDeltaT(dir, voxel_count);
    ctx.max_t = abs(edge_distance) * ctx.delta_t;
    ctx.voxel_coords = (floor(voxel_space_org) + vec3(0.5)) / voxel_count;
    ctx.voxel_step = ctx.step_sign / voxel_count;

    vec3 order = vec3(
        // ctx.max_t.y > ctx.max_t.x
        1.0 - step(ctx.max_t.y, ctx.max_t.x),
        // ctx.max_t.z > ctx.max_t.x
        1.0 - step(ctx.max_t.z, ctx.max_t.x),
        // ctx.max_t.z > ctx.max_t.y
        1.0 - step(ctx.max_t.z, ctx.max_t.y));

    ctx.mask.xyz = vec3(
        // X && Y
        order.x * order.y,
        // !X && Z
        (1.0 - order.x) * order.z,
        // (X && !Y) || (!X && Z)
        order.x * (1.0 - order.y) + (1.0 - order.x) * (1.0 - order.z)
        );

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
        // ctx.max_t.y > ctx.max_t.x
        1.0 - step(ctx.max_t.y, ctx.max_t.x),
        // ctx.max_t.z > ctx.max_t.x
        1.0 - step(ctx.max_t.z, ctx.max_t.x),
        // ctx.max_t.z > ctx.max_t.y
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
        //abs(sign(dot(ctx.voxel_step, ctx.mask.xyz))) *
        (1.0 - step(ctx.exit_value.x, ctx.voxel_coords.x * ctx.step_sign.x))
        * (1.0 - step(ctx.exit_value.y, ctx.voxel_coords.y * ctx.step_sign.y))
        * (1.0 - step(ctx.exit_value.z, ctx.voxel_coords.z * ctx.step_sign.z));
}

vec3 RecursionPointDDAContext(DDAContext ctx, vec3 org, vec3 dir, float region_size)
{
    return (org+dir*ctx.t) * region_size * (1.0 - EPSILON*ctx.step_sign)
        - floor(ctx.voxel_coords * region_size);
}

vec3 TextureCoordsDDAContext(DDAContext ctx)
{
    return ctx.voxel_coords;
}

uvec3 VoxelCoordsDDAContext(DDAContext ctx)
{
    return uvec3(floor(ctx.voxel_coords / abs(ctx.voxel_step)));
}

vec3 HitNormalDDAContext(DDAContext ctx)
{
    return ctx.mask.xyz * -ctx.step_sign;
}

float HitTDDAContext(DDAContext ctx)
{
    vec3 max_t = ctx.max_t - ctx.delta_t * ctx.mask.xyz;
    return max(0.0, min(max_t.x, min(max_t.y, max_t.z)));
}

struct DDAContextSave
{
    vec4 mask;
    vec3 voxel_coords;
    vec3 max_t;
};

vec3 TextureCoordsDDAContextSave(DDAContextSave ctx)
{
    return ctx.voxel_coords;
}

float HitTDDAContextSave(in DDAContextSave ctx, vec3 dir, float voxel_count)
{
    vec3 delta_t = InitDeltaT(dir, voxel_count);
    vec3 max_t = ctx.max_t - delta_t * ctx.mask.xyz * ctx.mask.w;
    return max(0.0, min(max_t.x, min(max_t.y, max_t.z)));
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
    ctx.delta_t = InitDeltaT(dir, voxel_count);
    ctx.voxel_step = ctx.step_sign / voxel_count;

    ctx.max_t = save.max_t;
    ctx.voxel_coords = save.voxel_coords;
    ctx.mask = save.mask;
    ctx.t = min(ctx.max_t.x, min(ctx.max_t.y, ctx.max_t.z));

    return ctx;
}
