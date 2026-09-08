#versione 450

const float base_radius = 6.360;
const float outer_radius = 6.460;

const float scale_factor = 1000000;

const vec3 rayleigh_scattering = vec3(5.802e-6, 13.56e-6, 33.10e-6) * scale_factor;
const float mie_scattering = 3.996e-6 * scale_factor;
const float mie_absorption = 4.40e-6 * scale_factor;
const vec3 ozone_absorption = vec3(0.65e-6, 1.881e-6, 0.085e-6) * scale_factor;

const float tangent_depth = sqrt(outer_radius*outer_radius - base_radius*base_radius);

float OuterCircleIntersection(float polar_radius, float outer_radius, float cos_theta)
{
    float b = polar_radius * cos_theta;
    float det = b*b - polar_radius*polar_radius + outer_radius*outer_radius;
    return -b + sqrt(det);
}

vec2 ZenithAltitudeMap(float altitude, float cos_zenith)
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

vec2 ZenithAltitudeInverseMap(vec2 uv)
{
    float polar_radius = sqrt(uv.y*uv.y*tangent_depth*tangent_depth + base_radius*base_radius);
    float horizon_depth = sqrt(polar_radius*polar_radius - base_radius*base_radius);
    float min_depth = outer_radius - polar_radius;
    float max_depth = horizon_depth + tangent_depth;
    float exit_depth = uv.x * (max_depth-min_depth) + min_depth;

    return vec2(
        polar_radius - base_radius,
        (-exit_depth + polar_radius*polar_radius - outer_radius*outer_radius)
        / (2.0 * exit_depth * polar_radius)
    );
}

void Transmittance(float org_height, float cos_theta)
{
}

void main()
{
    vec2 uv = vec2(gl_FragCoord.xy) / vec2(render_size.xy);
}
