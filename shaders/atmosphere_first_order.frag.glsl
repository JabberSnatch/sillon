#version 450

#extension GL_GOOGLE_include_directive: enable

layout(location = 0) in vec3 iFS_csPosition;
layout(location = 0) out vec4 oFS_color;
layout(location = 1) out vec4 oFS_baseline;

layout(set = 0, binding = 0) uniform sampler2D transmittance;

layout(push_constant, std430) uniform PushConstants {
    vec3 light_dir;
    float altitude;
    ivec2 render_size;
};

#include "atmosphere_parameters.glsl"

vec3 TransmittanceLookup(float altitude, float cos_zenith)
{
    vec2 uv = AltitudeZenithMap(altitude, cos_zenith);
    return texture(transmittance, uv).xyz;
}

vec3 Transmittance(float low, float cos_zenith, float high, float distance)
{
    float high_cos_zenith = (distance - (min(low, high) + base_radius) * abs(cos_zenith)) / (max(low, high) + base_radius);
    vec3 low_tr = TransmittanceLookup(min(low, high), abs(cos_zenith));
    vec3 high_tr = TransmittanceLookup(max(high, low), abs(high_cos_zenith));
    return low_tr / high_tr;
}

#define STEP_COUNT 128.0
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
        direct_radiance = vec3(0.0);
        step_size = exit_depth;
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

        vec3 tr_sky = TransmittanceLookup(sample_altitude, sample_cos_light);

        vec3 tr_source = Transmittance(altitude, cos_zenith, sample_altitude, sample_distance);
        vec3 tr_total = tr_sky * tr_source;

        total_inscattering += inscattering_factor * tr_total * step_size * 40.0;
    }

    //total_inscattering += direct_radiance;

    return total_inscattering;
}

vec3 FirstOrderIrradiance_baseline(float altitude,
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
        direct_radiance = vec3(0.0);
        step_size = exit_depth;
    }

    float dx = 1.0 / STEP_COUNT;
    step_size *= dx;

    vec3 optical_depths = vec3(0.0);
    vec3 inscattering = vec3(0.0);

    for (float index_primary = 0.0; index_primary < STEP_COUNT; ++index_primary)
    {
        vec3 step = ray_org + ray_dir * step_size * (index_primary + 1.0);
        float sample_polar = length(step);
        float sample_height = sample_polar - base_radius;

        vec3 densities = vec3(
            RayleighDensity(sample_height),
            MieDensity(sample_height),
            OzoneDensity(sample_height)
        );

        vec3 optical_step = densities * step_size;
        optical_depths += optical_step;

        float sample_cos_light = dot(normalize(step), light_dir);
        float sample_cos_horizon = -sqrt(1.0 - min(1.0, (base_radius*base_radius)/(sample_polar*sample_polar)));
        if (sample_cos_light < sample_cos_horizon)
            continue;

        vec3 light_optical_depths = vec3(0.0);
        float light_exit_depth =
            OuterCircleIntersection(sample_height + base_radius,
                                    outer_radius,
                                    dot(normalize(step), light_dir));

        float light_step_size = light_exit_depth * dx;
        for (float index_light = 0.0; index_light < STEP_COUNT; ++index_light)
        {
            vec3 light_step = step + light_dir * light_step_size * (index_light + 1.0);
            float light_height = length(light_step) - base_radius;

            vec3 light_densities = vec3(
                RayleighDensity(light_height),
                MieDensity(light_height),
                OzoneDensity(light_height)
            );

            light_optical_depths += light_densities * light_step_size;
        }

        vec3 current_depths = optical_depths + light_optical_depths;
        vec3 tr_sky = exp(
            -rayleigh_scattering * light_optical_depths.x
            -(mie_scattering+mie_absorption).xxx * light_optical_depths.y
            -ozone_absorption * light_optical_depths.z);

        vec3 tr_source = exp(
            -rayleigh_scattering * optical_depths.x
            -(mie_scattering+mie_absorption).xxx * optical_depths.y
            -ozone_absorption * optical_depths.z);

        vec3 tr_total = tr_sky * tr_source;

        vec3 transmittance = exp(
            -rayleigh_scattering * current_depths.x
            -(mie_scattering+mie_absorption).xxx * current_depths.y
            -ozone_absorption * current_depths.z);

        vec3 rayleigh_transmittance = densities.x * transmittance;
        vec3 mie_transmittance = densities.y * transmittance;

        inscattering += 40.0 * step_size *
            (rayleigh_scattering * RayleighPhase(cos_ray_light) * rayleigh_transmittance
             + mie_scattering * MiePhase(cos_ray_light) * mie_transmittance);

        //optical_depths += densities * step_size;
    }

    return inscattering;
    //return vec3(1.0) - exp(-inscattering);
}


void main()
{
    vec2 uv = (vec2(gl_FragCoord.xy) + vec2(0.5)) / vec2(render_size.xy);

    vec2 ray_dir_spherical = uv * 2.0 - vec2(1.0);
    vec3 ray_dir_world = vec3(
        cos(ray_dir_spherical.y) * cos(ray_dir_spherical.x),
        sin(ray_dir_spherical.y),
        cos(ray_dir_spherical.y) * sin(ray_dir_spherical.x)
    );

#if 1
    vec3 irradiance = FirstOrderIrradiance(altitude / scale_factor,
                                           ray_dir_world,
                                           light_dir);
    //irradiance *= 40.0; // intensity scale
    //irradiance = vec3(1.0) - exp(-irradiance); // tonemap
#endif

#if 1
    vec3 baseline = FirstOrderIrradiance_baseline(altitude / scale_factor,
                                                  ray_dir_world,
                                                  light_dir);
#endif

    oFS_color = vec4(irradiance, 1.0);
    oFS_baseline = vec4(baseline, 1.0);
}
