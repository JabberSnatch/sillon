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
    //vec2 low_uv = AltitudeZenithMap(low, cos_zenith);
    //vec2 high_uv = AltitudeZenithMap(high,cos_zenith);
    vec3 low_tr = TransmittanceLookup(low, cos_zenith);//texture(transmittance, low_uv).xyz;
    vec3 high_tr = TransmittanceLookup(high, cos_zenith);//texture(transmittance, high_uv).xyz;
    return low_tr / high_tr;
}

#define STEP_COUNT 128.0
vec3 FirstOrderIrradiance(float altitude, float cos_zenith, float cos_light)
{
    float polar_radius = altitude + base_radius;
    float exit_depth = OuterCircleIntersection(polar_radius, outer_radius, cos_zenith);

    float step_size = exit_depth / STEP_COUNT;
    vec3 total_inscattering = vec3(0.0);
    for (float sample_index = 0.0; sample_index < STEP_COUNT; ++sample_index)
    {
        float sample_altitude = altitude + step_size * cos_zenith * sample_index;

        vec3 rayleigh_inscattering =
            rayleigh_scattering * RayleighDensity(sample_altitude) * RayleighPhase(cos_light);
        vec3 mie_inscattering =
            mie_scattering.xxx * MieDensity(sample_altitude) * MiePhase(cos_light);

        vec3 inscattering_factor = rayleigh_inscattering + mie_inscattering;

        vec3 tr_source = Transmittance(altitude, sample_altitude, cos_zenith);
        vec3 tr_sky = TransmittanceLookup(sample_altitude, light_dir.y);

        total_inscattering += inscattering_factor * tr_source * tr_sky;
    }

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
    float cos_light = dot(ray_dir_world, light_dir);

    vec3 irradiance = FirstOrderIrradiance(altitude / scale_factor,
                                           cos_zenith,
                                           cos_light);

    oFS_color = vec4(irradiance, 1.0);
}
