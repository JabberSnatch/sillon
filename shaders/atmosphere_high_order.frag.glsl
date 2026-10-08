#version 450

#extension GL_GOOGLE_include_directive: enable

layout(location = 0) in vec3 iFS_csPosition;
layout(location = 0) out vec4 oFS_color;

layout(set = 0, binding = 0) uniform sampler2D transmittance;

layout(push_constant, std430) uniform PushConstants {
    ivec2 render_size;
};

#include "atmosphere_parameters.glsl"

vec3 FirstOrderIrradiance(float altitude,
                          vec3 ray_dir,
                          vec3 light_dir)
{
    return vec3(0.0);
}

#define SAMPLE_COUNT 64
vec3 TotalIrradiance(float altitude, float cos_light)
{
    vec3 light_dir = vec3(sqrt(1.0 - cos_light*cos_light), cos_light, 0.0);

    vec3 irradiance = vec3(0.0);
    vec3 transfer_factor = vec3(0.0);
    for (float sample_index = 0u; sample_index < SAMPLE_COUNT; ++sample_index)
    {
        vec3 sample_dir = vec3(0.0);

        vec3 sample_irradiance = FirstOrderIrradiance(altitude, sample_dir, light_dir);
        vec3 sample_transfer = vec3(0.0);

        irradiance += sample_irradiance / float(SAMPLE_COUNT);
        transfer_factor += sample_transfer / float(SAMPLE_COUNT);
    }

    return irradiance * transfer_factor;
}

void main()
{
    vec2 uv = (vec2(gl_FragCoord.xy) + vec2(0.5)) / vec2(render_size.xy);

    vec3 light_dir;

    oFS_color = vec4(1.0);
}
