#version 450

layout(location = 0) in vec3 iFS_csPosition;
layout(location = 0) out vec4 oFS_color;

layout(push_constant, std430) uniform PushConstants {
    ivec2 render_size;
};

const float base_radius = 6.360;
const float outer_radius = 6.460;

const float scale_factor = 1000000;

const vec3 rayleigh_scattering = vec3(5.802e-6, 13.56e-6, 33.10e-6) * scale_factor;
const float mie_scattering = 3.996e-6 * scale_factor;
const float mie_absorption = 4.40e-6 * scale_factor;
const vec3 ozone_absorption = vec3(0.65e-6, 1.881e-6, 0.085e-6) * scale_factor;

const float tangent_depth = sqrt(outer_radius*outer_radius - base_radius*base_radius);

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

float OuterCircleIntersection(float polar_radius, float outer_radius, float cos_zenith)
{
    float b = polar_radius * cos_zenith;
    float det = b*b - polar_radius*polar_radius + outer_radius*outer_radius;
    return -b + sqrt(det);
}

vec2 AltitudeZenithMap(float altitude, float cos_zenith)
{
    float polar_radius = altitude + base_radius;
    float horizon_depth = sqrt(polar_radius*polar_radius - base_radius*base_radius);
    float min_depth = outer_radius - polar_radius;
    float max_depth = horizon_depth + tangent_depth;
    float exit_depth = OuterCircleIntersection(polar_radius, outer_radius, cos_zenith);

    return vec2(
        (exit_depth - min_depth) / (max_depth - min_depth),
        horizon_depth / tangent_depth
    );
}

vec2 AltitudeZenithInverseMap(vec2 uv)
{
    float polar_radius = sqrt(uv.y*uv.y*tangent_depth*tangent_depth + base_radius*base_radius);
    float horizon_depth = sqrt(polar_radius*polar_radius - base_radius*base_radius);
    float min_depth = outer_radius - polar_radius;
    float max_depth = horizon_depth + tangent_depth;
    float exit_depth = uv.x * (max_depth-min_depth) + min_depth;

    return vec2(
        polar_radius - base_radius,
        -(exit_depth + polar_radius*polar_radius - outer_radius*outer_radius)
        / (2.0 * exit_depth * polar_radius)
    );
}

#define STEP_COUNT 512.0
vec3 Transmittance(float altitude, float cos_zenith)
{
    float polar_radius = altitude + base_radius;
    float exit_depth = OuterCircleIntersection(polar_radius, outer_radius, cos_zenith);

    vec3 optical_depths = vec3(0.0);

    float step_size = exit_depth / STEP_COUNT;
    for (float sample_index = 0.0; sample_index < STEP_COUNT; ++sample_index)
    {
        float sample_altitude = altitude + step_size * cos_zenith * sample_index;

        vec3 densities = vec3(
            RayleighDensity(sample_altitude),
            MieDensity(sample_altitude),
            OzoneDensity(sample_altitude)
        );
        vec3 optical_step = densities * step_size;

        optical_depths += optical_step;
    }

    vec3 total_extinction =
        rayleigh_scattering * optical_depths.x
        + (mie_scattering+mie_absorption).xxx * optical_depths.y
        + ozone_absorption * optical_depths.z;

    vec3 transmittance = exp(-total_extinction);

    return transmittance;
}

void main()
{
    vec2 uv = (vec2(gl_FragCoord.xy) + vec2(0.5)) / vec2(render_size.xy);
    vec2 alt_zen = AltitudeZenithInverseMap(uv);
    float altitude = alt_zen.x;
    float cos_zenith = alt_zen.y;

    vec3 tr = Transmittance(altitude, cos_zenith);

    oFS_color = vec4(tr, 1.0);
}
