#extension GL_EXT_shader_explicit_arithmetic_types_int64: enable

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

#if 0
#define FIXED_PRODUCT_TEMPLATE(ITYPE, UTYPE)                        \
    ITYPE FixedProduct(ITYPE a, ITYPE b)                            \
    {                                                               \
        ITYPE overflow_bit = (a | b) & ITYPE(0x40000000);           \
                                                                    \
        UTYPE al = UTYPE(a & ITYPE(0x7fff));                        \
        ITYPE ah = a >> ITYPE(15);                                  \
        UTYPE bl = UTYPE(b & ITYPE(0x7fff));                        \
        ITYPE bh = b >> ITYPE(15);                                  \
                                                                    \
        UTYPE low = al*bl;                                          \
        ITYPE mid_a = ITYPE(al)*bh + ITYPE(low >> 15);              \
        ITYPE mid_low = mid_a & ITYPE(0x7fff);                      \
        ITYPE mid_high = mid_a >> ITYPE(15);                        \
        mid_low = ITYPE(bl)*ah + mid_low;                           \
        ITYPE high = ah*bh;                                         \
                                                                    \
        return (high + (mid_low >> 15) + mid_high) | overflow_bit;  \
    }                                                               \

FIXED_PRODUCT_TEMPLATE(ivec3, uvec3)
FIXED_PRODUCT_TEMPLATE(ivec2, uvec2)
#endif

int OverflowBit(int a)
{
    return (abs(a) & 0x40000000) >> 30;
}

ivec3 FixedProduct(ivec3 a, ivec3 b)
{
    ivec3 overflow_bit = (abs(a) | abs(b)) & ivec3(0x40000000);

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

    return (high + (mid_low >> 15) + mid_high) | overflow_bit;
}

ivec2 FixedProduct(ivec2 a, ivec2 b)
{
    ivec2 overflow_bit = (abs(a) | abs(b)) & ivec2(0x40000000);

    uvec2 al = uvec2(a & ivec2(0x7fff));
    ivec2 ah = a >> ivec2(15);
    uvec2 bl = uvec2(b & ivec2(0x7fff));
    ivec2 bh = b >> ivec2(15);

    uvec2 low = al*bl;
    ivec2 mid_a = ivec2(al)*bh + ivec2(low >> 15);
    ivec2 mid_low = mid_a & ivec2(0x7fff);
    ivec2 mid_high = mid_a >> ivec2(15);
    mid_low = ivec2(bl)*ah + mid_low;
    ivec2 high = ah*bh;

    return (high + (mid_low >> 15) + mid_high) | overflow_bit;
}


ivec3 OverflowSelect(ivec3 a)
{
    return sign(a & ivec3(0x40000000));
}

struct FixedPointDDA {
    ivec3 current_position;

    //ivec3 current_voxel;

    //ivec3 step_sign;
    ivec3 direction;

#if 0
    ivec3 delta_t;
    ivec3 max_t;
#endif

    int log_size;
    ivec4 mask;

    ivec3 forg;

    vec3 rdelta_t;
    vec3 rmax_t;

#if 0
    vec3 dir;
    ivec3 fdir;
#endif
};

struct FixedPointDDABackup {
    ivec3 current_position;
    //ivec3 current_voxel;
    int log_size;

#if 0
    ivec3 max_t;
#endif

    ivec4 mask;

    vec3 rmax_t;

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
    return (vec3(FixedPointDDACurrentVoxel(dda)) + 0.5) / float(1 << dda.log_size);
}

ivec3 FixedQuotient(ivec3 a, ivec3 b)
{
    i64vec3 a64 = i64vec3(a.x, a.y, a.z);
    i64vec3 b64 = i64vec3(b.x, b.y, b.z);

    i64vec3 int0 = a64 << i64vec3(30);
    i64vec3 int1 = int0 & i64vec3(0xffffffff80000000l);
    i64vec3 result = int1 / b64;
    ivec3 overflow_bit = ivec3(abs(result.x) >= FIXED_PRECISION,
                               abs(result.y) >= FIXED_PRECISION,
                               abs(result.z) >= FIXED_PRECISION);

    int resx = int(result.x) | (FIXED_PRECISION * overflow_bit.x);
    int resy = int(result.y) | (FIXED_PRECISION * overflow_bit.y);
    int resz = int(result.z) | (FIXED_PRECISION * overflow_bit.z);

    return ivec3(resx, resy, resz);
}

int BoxTest(int d, ivec2 dir, ivec2 org, int radius)
{
    ivec2 product = FixedProduct(dir, d.xx);
    ivec2 sum = org + product;
    return (OverflowBit(d) == 0
            && (d >= 0)
            && all(lessThan(abs(sum), ivec2(radius))))
        ? 1
        : 0;
}

float BoxTest(float d, vec2 dir, vec2 org, float radius)
{
    vec2 product = dir * d.xx;
    vec2 sum = org + product;
    return ((d >= 0.0) && all(lessThan(abs(sum), vec2(radius)))) ? 1.0 : 0.0;
}

ivec3 FixedPointDDAHitP(FixedPointDDA dda)
{
    ivec3 forg = dda.forg;
    ivec3 fdir = dda.direction;

    ivec3 current_voxel = FixedPointDDACurrentVoxel(dda);

#if 0
    int voxel_size = FixedPointDDAPrecision(dda);
    ivec3 sign_bit = (-sign(fdir) + 1) >> 1; // -1, 1 > 0, 1
    ivec3 voxel_faces = (current_voxel + sign_bit) * voxel_size;
    ivec3 offset = voxel_faces - forg;
#endif

    int voxel_radius = FixedPointDDAPrecision(dda) >> 1;
    ivec3 sign_bit = -sign(fdir);
    ivec3 voxel_center = ((current_voxel << 1) + 1) * voxel_radius;
    forg -= voxel_center;
    ivec3 face_distance = voxel_radius * sign_bit - forg;

    //ivec3 d = ivec3(((i64vec3(face_distance) << 30) & i64vec3(0xffffffff80000000l)) / i64vec3(fdir));
#define FACTOR 0
    ivec3 d = FixedQuotient(face_distance >> FACTOR, fdir);

    //#define TEST(U, VW) (d.U >= 0) && all(lessThan(abs(forg.VW + FixedProduct(fdir.VW, (d.U).xx)), ivec2(voxel_radius)))
#define TEST(U, VW) BoxTest(d.U, fdir.VW, forg.VW >> FACTOR, voxel_radius >> FACTOR)

    ivec3 test = ivec3(int(TEST(x, yz)), int(TEST(y, zx)), int(TEST(z, xy)));

#undef TEST

    ivec3 masked_distance = d * test;//(sign_bit * test);
    int distance = masked_distance.x +
        (1 - test.x) * (masked_distance.y +
                        (1 - test.y) * masked_distance.z);

    vec3 check_dda_org = FixedToFloat(dda.forg);
    vec3 check_org = FixedToFloat(forg);
    vec3 check_dir = FixedToFloat(fdir);
    float check_voxel_radius = FixedToFloat(voxel_radius.xxx).x;
    vec3 check_face_distance = check_voxel_radius * vec3(sign_bit) - check_org;
    vec3 check_d = check_face_distance / check_dir;

#define TEST(U, VW) BoxTest(check_d.U, check_dir.VW, check_org.VW, check_voxel_radius)

    vec3 check_test = vec3(TEST(x, yz), TEST(y, zx), TEST(z, xy));

#undef TEST

    vec3 check_masked_distance = check_d * check_test;//(vec3(sign_bit) * check_test);
    float check_distance = check_masked_distance.x +
        (1.0 - check_test.x) * (check_masked_distance.y +
                        (1.0 - check_test.y) * check_masked_distance.z);

    vec3 check_dir_distance = check_dir * check_distance;
    vec3 check_result = check_dda_org + check_dir * check_distance;

    ivec3 dir_distance = FixedProduct(fdir, distance.xxx);
    ivec3 result = dda.forg + (FixedProduct(fdir, distance.xxx) << FACTOR);
    vec3 result_float = FixedToFloat(result);
    float distance_float = FixedToFloat(distance.xxx).x * pow(2.0, float(FACTOR));
    vec3 dir_distance_float = FixedToFloat(dir_distance) * pow(2.0, float(FACTOR));

    return dda.forg + (FixedProduct(fdir, distance.xxx) << FACTOR);
}

vec3 FixedPointDDAHitNormal(FixedPointDDA dda)
{
    int voxel_size = FixedPointDDAPrecision(dda);
    ivec3 current_voxel = FixedPointDDACurrentVoxel(dda);
    ivec3 current_position = FixedPointDDAHitP(dda);
    ivec3 offset = abs(current_position - (current_voxel * voxel_size + voxel_size / 2));

    ivec3 normal = ivec3(
        greaterThanEqual(offset, ivec3(max(offset.x, max(offset.y, offset.z)))));

    return vec3(-normal * sign(dda.direction));
}

float FixedPointDDAHitT(FixedPointDDA dda)
{
    vec3 ray = FixedToFloat(dda.current_position - dda.forg);
    return length(ray);
}

float FixedPointDDABackupHitT(FixedPointDDABackup dda)
{
    vec3 ray = FixedToFloat(dda.current_position - dda.forg);
    return length(ray);
}

ivec3 FixedPointDDABackupCurrentVoxel(FixedPointDDABackup dda)
{
    return dda.current_position >> (FIXED_BIT_COUNT - dda.log_size);
}

int FixedPointDDABackupPrecision(FixedPointDDABackup dda)
{
    return FIXED_PRECISION >> dda.log_size;
}

vec3 FixedPointDDABackupTextureCoords(FixedPointDDABackup dda)
{
    return (vec3(FixedPointDDABackupCurrentVoxel(dda)) + 0.5) / float(1 << dda.log_size);
}

ivec3 FixedPointDDAOrderMask(FixedPointDDA dda)
{
#if 0
    ivec3 order = ivec3(
        1 - step(dda.max_t.y, dda.max_t.x),
        1 - step(dda.max_t.z, dda.max_t.x),
        1 - step(dda.max_t.z, dda.max_t.y));
#else
    ivec3 order = ivec3(
        1 - step(dda.rmax_t.y, dda.rmax_t.x),
        1 - step(dda.rmax_t.z, dda.rmax_t.x),
        1 - step(dda.rmax_t.z, dda.rmax_t.y));
#endif

    return ivec3(
        order.x * order.y,
        (1 - order.x) * order.z,
        order.x * (1 - order.y) + (1 - order.x) * (1 - order.z)
    );
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
        //ivec3 exit_value = ((voxel_precision * step_sign) >> 1) + (voxel_precision >> 1);
        ivec3 exit_value = ((voxel_mask * step_sign) + voxel_mask) >> 1;
        ivec3 edge_distance = (exit_value - (step_sign * forg_subvoxel)) << ctx.log_size;
        //ivec3 edge_distance = (exit_value - (step_sign * forg_subvoxel));
        float voxel_count = float(1 << ctx.log_size);

#if 0
        //ctx.delta_t = FloatToFixed(min(1.0 / (abs(dir)), vec3(1.0)));
        ctx.delta_t = FloatToFixed(min(1.0 / (abs(dir) * voxel_count), vec3(1.0)));
        //ctx.delta_t = FloatToFixed(1.0 / (abs(dir) * (voxel_count * 1024)));
        ctx.max_t = FixedProduct(edge_distance, ctx.delta_t);
#endif

        ctx.rdelta_t = 1.0 / (abs(dir) * voxel_count);
        ctx.rmax_t = FixedToFloat(edge_distance) * ctx.rdelta_t;
    }

    ctx.mask.xyz = FixedPointDDAOrderMask(ctx);

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
    ctx.mask.xyz = FixedPointDDAOrderMask(ctx);

    ivec3 step_sign = sign(ctx.direction);
    //ctx.current_voxel += ctx.mask.xyz * step_sign;

#if 0
    ivec3 min_t = ctx.mask.xyz * ctx.max_t;
    int step_factor = min_t.x + min_t.y + min_t.z;
    ivec3 step = ctx.mask.xyz * ctx.delta_t;
    ctx.max_t += step - step_factor * OverflowSelect(ctx.delta_t);
#endif

    vec3 rmin_t = ctx.mask.xyz * ctx.rmax_t;
    float rstep_factor = rmin_t.x + rmin_t.y + rmin_t.z;
    vec3 rstep = ctx.mask.xyz * ctx.rdelta_t;
    ctx.rmax_t += rstep - rstep_factor.xxx;

    ivec3 offset =
        //(FixedProduct(step_factor.xxx, ctx.direction) * (1 - ctx.mask.xyz)) << 10;
        //(FixedProduct(step_factor.xxx, ctx.direction) * (1 - ctx.mask.xyz));
        (FixedProduct(FloatToFixed(rstep_factor.xxx), ctx.direction) * (1 - ctx.mask.xyz));
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
    save.log_size = ctx.log_size;
    save.mask = ctx.mask;

#if 0
    save.max_t = ctx.max_t;
#endif

    save.rmax_t = ctx.rmax_t;

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

    //float voxel_count = float(1 << log_voxel_count);
    float voxel_count = float(1 << save.log_size);
#if 0
    ctx.delta_t = FloatToFixed(min(1.0 / (abs(dir) * voxel_count), vec3(1.0)));
    ctx.max_t = save.max_t;
#endif

    ctx.rdelta_t = 1.0 / (abs(dir) * voxel_count);
    ctx.rmax_t = save.rmax_t;
    //ctx.size = 1 << log_voxel_count;
    //ctx.log_size = log_voxel_count;
    ctx.log_size = save.log_size;
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
