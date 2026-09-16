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

float InnerCircleIntersection(float polar_radius, float inner_radius, float cos_zenith)
{
    float b = polar_radius * cos_zenith;
    float det = b*b - polar_radius*polar_radius + inner_radius*inner_radius;
    return -b - sqrt(det);
}

float SampleAltitude(float base_altitude, float distance, float cos_zenith)
{
    return sqrt(base_altitude*base_altitude
                + distance*distance
                - 2.0*distance*base_altitude*cos_zenith);
}

float SampleLocalAngle(float base_altitude, float distance, float cos_zenith)
{
    return (distance - base_altitude*cos_zenith) / SampleAltitude(base_altitude, distance, cos_zenith);
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
