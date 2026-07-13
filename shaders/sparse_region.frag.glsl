#version 450

#extension GL_GOOGLE_include_directive: enable
#extension GL_EXT_nonuniform_qualifier: enable

#include "voxel_traversal.glsl"

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
    vec3 region_scale;
    float node_size;
    int stack_depth;
    uint sample_offset;
    uint leaves_begin;
    float sun_power;
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
vec2 pixelPlasticSequence(uint i) { return 1.05 * fract(uint2float(plasticSequence(i)) + vec2(0.5)) -  vec2(0.5); }
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

vec4 RegionTraversal4(vec3 org, vec3 dir, inout vec3 payload)
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
                        payload = vec3(0.0);
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
                        vec3 chunk_coords =
                            (vec3(int_coords) + vec3(0.5)) / (CHUNK_SIZE * node_size);
                        payload = texture(payloads[nonuniformEXT(current_tex - leaves_begin)],
                                          ctx_cache[depth-1].voxel_coords - vec3(0.5) / node_size + chunk_coords).xyz;
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

float RayleighPhase(float cos_th) {
    return 3.0 * (1 + cos_th*cos_th) / (16.0 * 3.14159265);
}

float RayleighDensity(float h) {
    return exp(-h/0.008);
}

float MiePhase(float cos_th) {
    const float g = 0.8;
    // Henyey-Greenstein phase func
    return (1 - g*g) / (pow(1 + g*g - 2*g*cos_th, 1.5) * 4 * 3.14159265);
}

float MieDensity(float h) {
    return exp(-h/0.0012);
}

float OzonePhase() {
    return 1 / (4.0 * 3.14159265);
}

float OzoneDensity(float h) {
    return max(0.0, 1.0 - abs(h - 0.025)/0.015);
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
vec3 AtmosphericScattering(vec3 position, vec3 direction)
{
    const float base_radius = 6.360;
    const float outer_radius = 6.460;

    const float scale_factor = 1000000;

    const vec3 rayleigh_scattering = vec3(5.802e-6, 13.56e-6, 33.10e-6) * scale_factor;
    const float mie_scattering = 3.996e-6 * scale_factor;
    const float mie_absorption = 4.40e-6 * scale_factor;
    const vec3 ozone_absorption = vec3(0.65e-6, 1.881e-6, 0.085e-6) * scale_factor;

    const float light_angle = dot(direction, light_dir);

    vec3 origin = vec3(0.0, base_radius + 0.000001, 0.0) + position.xyz / scale_factor;
    float altitude = origin.y;

    vec3 primary_ground_hit =
        CircleIntersection(base_radius, altitude, direction.y);
    if (primary_ground_hit.x > 0.0)
        return vec3(0.1);

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

        inscattering += (20.0 + pow(20.0, sun_power*2.0+1.0)) * step_size *
            (rayleigh_scattering * RayleighPhase(light_angle) * rayleigh_transmittance
             + mie_scattering * MiePhase(light_angle) * mie_transmittance);

        optical_depths += densities * step_size;
    }

    vec3 sky_color = vec3(1.0) - exp(-1.0 * inscattering);
    if (sky_color != sky_color)
        sky_color = vec3(1.0, 0.0, 1.0);
    return sky_color;
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
        inverse(mat3(view_model)) *
        inverse(mat3(projection))
        * vec3(uv * 2.0 - vec2(1.0), 1.0);
    dir = normalize(dir);

    vec3 step_sign = sign(-dir);
    vec3 max_t = abs(((step_sign * 0.5 + vec3(0.5)) - (step_sign * local_coords)) / dir);

    float entry_depth = distance(local_camera, local_coords);
    float region_begin = min(max_t.x, min(max_t.y, max_t.z));

    vec3 root_org = local_camera;
    if (root_org != clamp(root_org, vec3(0.0), vec3(1.0) * box_scale))
    {
#if 0
        root_org = local_coords
            - dir * 0.00001
            - (dir * min(entry_depth, region_begin));
#endif

        vec3 alt_org = root_org - vec3(0.5) * box_scale;
        vec3 abs_org = abs(alt_org) * (2.0 / box_scale);
        float winding = max(abs_org.x, max(abs_org.y, abs_org.z)) < 1.0 ? -1.0 : 1.0;
        vec3 sign_bit = -sign(dir);
        vec3 d = (0.5 * box_scale * winding * sign_bit - alt_org) / dir;

#define TEST(U, VW) (d.U > 0.0) && all(lessThan(abs(alt_org.VW + dir.VW * d.U), vec2(0.5 * box_scale)))

        bvec3 test = bvec3(TEST(x, yz), TEST(y, zx), TEST(z, xy));
        sign_bit = test.x
            ? vec3(sign_bit.x, 0.0, 0.0)
            : (test.y
                ? vec3(0.0, sign_bit.y, 0.0)
                : vec3(0.0, 0.0, test.z ? sign_bit.z : 0.0));
#undef TEST

        float distance = (sign_bit.x != 0.0) ? d.x : ((sign_bit.y != 0.0) ? d.y : d.z);
        root_org = local_camera + dir * distance;
    }

    entry_depth = max(entry_depth - region_begin, 0.0);

    vec3 base_color = vec3(0.0);
    vec3 org = root_org;

    vec4 trav = RegionTraversal4(org, dir, base_color);
    vec3 payload = base_color;
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
        vec3 diffuse = vec3(0.0);

        vec3 indirect = vec3(0.0);
        vec3 exit_color = vec3(0.0);
        vec3 avg_dir = vec3(0.0);
        float avg_t = 0.0;
        if (display_mode == 0.0)
        {
            shadow = RegionTraversal4(hitp + light_dir*0.0001, light_dir, payload).w >= 0.0 ? 0.0 : 1.0;
            diffuse = max(0.0, dot(light_dir, trav.xyz)) * AtmosphericScattering(hitp, light_dir);
        }
        else
        {
            vec4 previous_hit = trav;
            vec3 previous_org = org;
            vec3 previous_dir = dir;
            vec3 current_weight = vec3(1.0) * payload;

#if 1
#define SAMPLE_COUNT 4
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
                            * AtmosphericScattering(current_org, light_dir);
                    }
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

                if (dot(current_ray, previous_hit.xyz) < 0.0)
                    break;

                vec3 next_payload = vec3(0.0);
                vec4 current_hit = RegionTraversal4(current_org, current_ray, next_payload);
                float next_weight = max(0.0, dot(-previous_dir, current_ray));
                if (current_hit.w < 0.0)
                {
                    exit_color =
                        AtmosphericScattering(current_org, current_ray) * current_weight * next_weight;
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
        color.xyz = AtmosphericScattering(org, dir);
    color.w = 1.0;
}
