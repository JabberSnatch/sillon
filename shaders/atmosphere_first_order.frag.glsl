#version 450

#extension GL_GOOGLE_include_directive: enable

layout(location = 0) in vec3 iFS_csPosition;
layout(location = 0) out vec4 oFS_color;

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

vec3 Transmittance(float low, float high, float cos_zenith)
{
    vec3 low_tr = TransmittanceLookup(min(low, high), abs(cos_zenith));
    vec3 high_tr = TransmittanceLookup(max(high, low), abs(cos_zenith));
    return low_tr / high_tr;
}

#define STEP_COUNT 128.0
vec3 FirstOrderIrradiance(float altitude,
                          float cos_zenith,
                          float cos_ray_light,
                          vec3 origin,
                          vec3 ray_dir)
{
    float polar_radius = altitude + base_radius;
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
        //cos_zenith = -cos_zenith;
        exit_depth = OuterCircleIntersection(base_radius, polar_radius, -cos_zenith);
        direct_radiance = vec3(0.0);
#if 0
        vec3(0.3) *
            Transmittance(0.0, altitude, -cos_zenith)
            * Transmittance(0.0, outer_radius, cos_ray_light);
#endif
        step_size = exit_depth;
    }

    float dx = 1.0 / STEP_COUNT;
    step_size *= dx;
    vec3 total_inscattering = vec3(0.0);
    for (float sample_index = 0.0; sample_index < STEP_COUNT; ++sample_index)
    {
        float sample_polar = SampleAltitude(polar_radius,
                                            step_size*(sample_index+1.0),
                                            cos_zenith);
        float sample_altitude = sample_polar - base_radius;

        vec3 rayleigh_inscattering =
            rayleigh_scattering * RayleighDensity(sample_altitude) * RayleighPhase(cos_ray_light);
        vec3 mie_inscattering =
            mie_scattering.xxx * MieDensity(sample_altitude) * MiePhase(cos_ray_light);

        vec3 inscattering_factor = rayleigh_inscattering + mie_inscattering;

        vec3 tr_source = Transmittance(altitude, sample_altitude, cos_zenith);

#if 0
        float light_exit_depth = OuterCircleIntersection(
            sample_polar,
            outer_radius,
            light_dir.y);
        float sample_cos_light = (outer_radius*outer_radius - light_exit_depth*light_exit_depth - sample_polar*sample_polar) / (2.0*light_exit_depth*sample_polar);
#endif
        float sample_cos_light = dot(normalize(origin+ray_dir*step_size*(sample_index+1.0)), light_dir);
        vec3 tr_sky = TransmittanceLookup(sample_altitude, sample_cos_light);

        total_inscattering += inscattering_factor * tr_source * tr_sky * step_size;
    }

    //total_inscattering += direct_radiance;

    return total_inscattering;
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

    float cos_zenith = ray_dir_world.y;
    float cos_light = dot(-ray_dir_world, light_dir);

    vec3 irradiance = FirstOrderIrradiance(altitude / scale_factor,
                                           cos_zenith,
                                           cos_light,
                                           vec3(0.0, altitude/scale_factor, 0.0),
                                           ray_dir_world);

    oFS_color = vec4(irradiance, 1.0);
}
