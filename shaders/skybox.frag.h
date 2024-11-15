R"_(
layout(location = 0) in vec3 iFS_csPosition;
layout(location = 0) out vec4 oFS_color;
layout(push_constant, std430) uniform PushConstant {
    mat4 projection;
    mat4 view;
    vec4 position;
    vec3 light_dir;
};

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

#define STEP_COUNT 64.0
#define STEP_COUNT_SECONDARY 32.0
void main()
{
    const float base_radius = 6.360;
    const float outer_radius = 6.460;

    const float scale_factor = 1000000;

    const vec3 rayleigh_scattering = vec3(5.802e-6, 13.56e-6, 33.10e-6) * scale_factor;
    const float mie_scattering = 3.996e-6 * scale_factor;
    const float mie_absorption = 4.40e-6 * scale_factor;
    const vec3 ozone_absorption = vec3(0.65e-6, 1.881e-6, 0.085e-6) * scale_factor;

    vec3 dir = normalize(inverse(mat3(view)) * inverse(mat3(projection)) * iFS_csPosition);
    float light_angle = dot(dir, light_dir);

    vec3 origin = vec3(0.0, base_radius, 0.0) + position.xyz * 0.000001;
    float altitude = origin.y;

    vec3 primary_ground_hit =
        CircleIntersection(base_radius, altitude, dir.y);
    if (primary_ground_hit.x > 0.0)
    {
        oFS_color = vec4(vec3(0.1), 1.0);
        return;
    }

    vec3 primary_atmosphere_hit =
        CircleIntersection(outer_radius, altitude, dir.y);
    if (primary_atmosphere_hit.x <= 0.0)
    {
        oFS_color = vec4(dir * 0.5 + vec3(0.5), 1.0);
        return;
    }

    float step_size = primary_atmosphere_hit.z / STEP_COUNT;

    vec3 optical_depths = vec3(0.0);
    vec3 inscattering = vec3(0.0);

    for (float index_primary = 0.0; index_primary < STEP_COUNT; ++index_primary)
    {
        vec3 step = origin + step_size * (index_primary + 0.5) * dir;
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

        inscattering += 40.0 * step_size *
            (rayleigh_scattering * RayleighPhase(light_angle) * rayleigh_transmittance
             + mie_scattering * MiePhase(light_angle) * mie_transmittance);

        optical_depths += densities * step_size;
    }

    vec3 sky_color = vec3(1.0) - exp(-1.0 * inscattering);
    oFS_color = vec4(sky_color, 1.0);
    if (sky_color != sky_color)
        oFS_color = vec4(1.0, 0.0, 1.0, 1.0);
    //oFS_color = vec4(RayleighPhase(light_angle).xxx * 0.5 + 0.5, 1.0);
    //oFS_color = vec4(dot(sky_color, vec3(1.0/3.0)).xxx, 1.0);
    //oFS_color = vec4(inscattering, 1.0);
}
)_"
