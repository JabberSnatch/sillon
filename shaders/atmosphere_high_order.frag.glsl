#version 450

#extension GL_GOOGLE_include_directive: enable

layout(location = 0) in vec3 iFS_csPosition;
layout(location = 0) out vec4 oFS_color;

layout(set = 0, binding = 0) uniform sampler2D transmittance;

layout(push_constant, std430) uniform PushConstants {
    ivec2 render_size;
};

#include "atmosphere_parameters.glsl"
#include "sampling.glsl"

#define STEP_COUNT 32
vec3 FirstOrderIrradiance(float altitude,
                          vec3 ray_dir,
                          vec3 light_dir)
{
    float polar_radius = altitude + base_radius;
    vec3 ray_org = vec3(0.0, polar_radius, 0.0);
    float cos_zenith = ray_dir.y;
    float cos_ray_light = dot(ray_dir, light_dir);

    float exit_depth = 0.0;
    vec3 direct_radiance = vec3(0.0);
    float step_size = 0.0;
    float cos_horizon = -sqrt(1.0 - (base_radius*base_radius)/(polar_radius*polar_radius));

    if (cos_zenith > cos_horizon)
    {
        exit_depth = OuterCircleIntersection(polar_radius, outer_radius, cos_zenith);
        step_size = exit_depth;
        direct_radiance = vec3(0.0);
    }
    else
    {
        exit_depth = OuterCircleIntersection(base_radius, polar_radius, -cos_zenith);
        step_size = exit_depth;

#if 1
        vec3 ground_point = ray_org + ray_dir * exit_depth;
        float ground_cos_light = dot(normalize(ground_point), light_dir);

        direct_radiance = TransmittanceLookup(0.0, ground_cos_light, transmittance)
            * Transmittance(0.0, -cos_zenith, altitude, exit_depth, transmittance)
            * vec3(0.05) * ground_cos_light * 40.0;
#endif
        //direct_radiance = vec3(0.0);
    }

    float dx = 1.0 / STEP_COUNT;
    step_size *= dx;
    vec3 total_inscattering = vec3(0.0);
    for (float sample_index = 0.0; sample_index < STEP_COUNT; ++sample_index)
    {
        float sample_distance = step_size*(sample_index+1.0);
        float sample_polar = SamplePolarRadius(polar_radius,
                                               sample_distance,
                                               -cos_zenith);
        float sample_altitude = sample_polar - base_radius;

        vec3 rayleigh_inscattering =
            rayleigh_scattering * RayleighDensity(sample_altitude) * RayleighPhase(cos_ray_light);
        vec3 mie_inscattering =
            mie_scattering.xxx * MieDensity(sample_altitude) * MiePhase(cos_ray_light);

        vec3 inscattering_factor = rayleigh_inscattering + mie_inscattering;

        float sample_cos_light = dot(normalize(ray_org+ray_dir*sample_distance), light_dir);
        float sample_cos_horizon = -sqrt(1.0 - min(1.0, (base_radius*base_radius)/(sample_polar*sample_polar)));
        if (sample_cos_light < sample_cos_horizon)
            continue;

        vec3 tr_sky = TransmittanceLookup(sample_altitude, sample_cos_light, transmittance);

        vec3 tr_source = (cos_zenith > cos_horizon)
            ? Transmittance(altitude, cos_zenith, sample_altitude, sample_distance, transmittance)
            : Transmittance(sample_altitude, -cos_zenith, altitude, sample_distance, transmittance);

        vec3 tr_total = tr_sky * tr_source;

        total_inscattering += inscattering_factor * tr_total * step_size * 40.0;
    }

    total_inscattering += direct_radiance;

    return total_inscattering;
}

vec3 InscatteringPotential(float altitude,
                           vec3 ray_dir,
                           vec3 light_dir)
{
    float polar_radius = altitude + base_radius;
    vec3 ray_org = vec3(0.0, polar_radius, 0.0);
    float cos_zenith = ray_dir.y;
    float cos_ray_light = dot(ray_dir, light_dir);

    float exit_depth = 0.0;
    vec3 direct_radiance = vec3(0.0);
    float step_size = 0.0;
    float cos_horizon = -sqrt(1.0 - (base_radius*base_radius)/(polar_radius*polar_radius));

    if (cos_zenith > cos_horizon)
    {
        exit_depth = OuterCircleIntersection(polar_radius, outer_radius, cos_zenith);
        step_size = exit_depth;
        direct_radiance = vec3(0.0);
    }
    else
    {
        exit_depth = OuterCircleIntersection(base_radius, polar_radius, -cos_zenith);
        step_size = exit_depth;
        direct_radiance = vec3(0.0);
    }

    float dx = 1.0 / STEP_COUNT;
    step_size *= dx;
    vec3 total_potential = vec3(0.0);
    for (float sample_index = 0.0; sample_index < STEP_COUNT; ++sample_index)
    {
        float sample_distance = step_size*(sample_index+1.0);
        float sample_polar = SamplePolarRadius(polar_radius,
                                               sample_distance,
                                               -cos_zenith);
        float sample_altitude = sample_polar - base_radius;

        vec3 rayleigh_inscattering =
            rayleigh_scattering * RayleighDensity(sample_altitude);
        vec3 mie_inscattering =
            mie_scattering.xxx * MieDensity(sample_altitude);

        vec3 inscattering_factor = rayleigh_inscattering + mie_inscattering;

        vec3 tr_source = (cos_zenith > cos_horizon)
            ? Transmittance(altitude, cos_zenith, sample_altitude, sample_distance, transmittance)
            : Transmittance(sample_altitude, -cos_zenith, altitude, sample_distance, transmittance);

        vec3 tr_total = tr_source;

        total_potential += inscattering_factor * tr_total * step_size;
    }

    return total_potential;
}

vec3 SphereSample(float index)
{
    vec2 owen_sample = OwenPlastic(uint(index), 0);
    float cos_theta = owen_sample.x * 2.0 - 1.0;
    float sin_theta = sqrt(1.0 - cos_theta*cos_theta);
    float phi = owen_sample.y * 3.14159265 * 2.0;
    vec3 ts_sample = vec3(
        cos(phi) * sin_theta,
        sin(phi) * sin_theta,
        cos_theta
    );

    return ts_sample;
}

#define SAMPLE_COUNT 64
vec3 TotalIrradiance(float altitude, float cos_light)
{
    vec3 light_dir = vec3(sqrt(1.0 - cos_light*cos_light), cos_light, 0.0);

    vec3 irradiance = vec3(0.0);
    vec3 transfer_factor = vec3(0.0);
    for (float sample_index = 0u; sample_index < SAMPLE_COUNT; ++sample_index)
    {
        vec3 sample_dir = SphereSample(sample_index);

        vec3 sample_irradiance = FirstOrderIrradiance(altitude, sample_dir, light_dir);
        vec3 sample_transfer = InscatteringPotential(altitude, sample_dir, light_dir);

        irradiance += sample_irradiance / float(SAMPLE_COUNT);
        transfer_factor += sample_transfer / float(SAMPLE_COUNT);
    }

    return irradiance * transfer_factor;
}

void main()
{
    vec2 uv = (vec2(gl_FragCoord.xy) + vec2(0.5)) / vec2(render_size.xy);

    float altitude = uv.y * (outer_radius - base_radius);
    float cos_light = uv.x * 2.0 - 1.0;

    vec3 irradiance = TotalIrradiance(altitude, cos_light);

    oFS_color = vec4(irradiance, 1.0);
}
