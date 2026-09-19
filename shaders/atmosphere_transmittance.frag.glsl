#version 450

#extension GL_GOOGLE_include_directive: enable

layout(location = 0) in vec3 iFS_csPosition;
layout(location = 0) out vec4 oFS_color;

layout(push_constant, std430) uniform PushConstants {
    ivec2 render_size;
};

#include "atmosphere_parameters.glsl"

#define STEP_COUNT 128.0
vec3 Transmittance(float altitude, float cos_zenith)
{
    float polar_radius = altitude + base_radius;
    float exit_depth = OuterCircleIntersection(polar_radius, outer_radius, cos_zenith);

    vec3 optical_depths = vec3(0.0);

    float step_size = exit_depth / STEP_COUNT;
    for (float sample_index = 0.0; sample_index < STEP_COUNT; ++sample_index)
    {
        float sample_polar = SamplePolarRadius(polar_radius,
                                               step_size*(sample_index+1.0),
                                               -cos_zenith);
        float sample_altitude = sample_polar - base_radius;

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
