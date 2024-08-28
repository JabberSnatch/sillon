R"(

#extension GL_EXT_nonuniform_qualifier: enable

layout(location = 0) in vec3 iFS_volume_coords;
layout(location = 1) in vec3 iFS_camera_position;
layout(location = 0) out vec4 color;
layout(set = 0, binding = 0) uniform usampler3D region;
layout(set = 0, binding = 2) uniform sampler3D chunks[];

layout(std140, set = 0, binding = 1) uniform MatrixBlock {
    mat4 projection;
    mat4 view_model;
    mat4 view;
    ivec2 screen_size;
};

#define REGION_SIZE 32.0
#define SECTION_SIZE 16.0

vec3 RegionTraversal(vec3 org, vec3 dir, out vec3 n)
{
    float d = 0.0;
    float t = 0.0;

    DDAContext ctx = InitDDAContext(org, dir, REGION_SIZE);

    bool voxel_rendered = false;
    DDAContext ctx_cache;
    vec3 org_cache;

    while (!voxel_rendered && ctx.mask.w == 1.0)
    {

        bool chunk_rendered = false;
        uint chunk_index = 0xffff;
        while (ctx.mask.w == 1.0)
        {
            uvec2 data = texture(region, ctx.voxel_coords).xy;
            if (data.y < 0x8000)
            {
                ctx_cache = ctx;
                org_cache = org;

                t = ctx.t;

                vec3 hit_pos = (org + dir*ctx.t);
                vec3 section_org =
                    hit_pos * REGION_SIZE * (1.0 - 0.00001*ctx.step_sign)
                    - floor(ctx.voxel_coords * REGION_SIZE);
                org = section_org;

                ctx = InitDDAContext(org, dir, SECTION_SIZE);
                ctx.mask.xyz = ctx_cache.mask.xyz;
                chunk_rendered = true;
                chunk_index = data.y;

                break;
            }

            if ((data.y & 0xff00) == 0x8000)
            {
                float step = float(data.y & 0x00ff) / 255.0;
                if (step > 0.0)
                {
                    chunk_rendered = true;
                    voxel_rendered = true;
                    break;
                }
            }

            chunk_index = data.y;
            float step = (float(data.x) / 65535.0) * ctx.mask.w;
            StepDDAContext(ctx);

            d += step;
        }

        if (chunk_rendered)
        {
            while (!voxel_rendered && ctx.mask.w == 1.0)
            {
                float step = texture(chunks[chunk_index], ctx.voxel_coords).x;
                if (step > 0.0)
                {
                    voxel_rendered = true;
                    t += ctx.t / REGION_SIZE;
                    break;
                }
                StepDDAContext(ctx);
            }
        }

        if (chunk_rendered && !voxel_rendered)
        {
            ctx = ctx_cache;
            org = org_cache;
            StepDDAContext(ctx);
        }
    }

    n = ctx.mask.xyz * -ctx.step_sign;
    return vec3(voxel_rendered ? 1.0 : 0.0, d, t);
}

void main() {
    vec2 uv = vec2(gl_FragCoord.x, gl_FragCoord.y) / vec2(screen_size.x, screen_size.y);
    vec3 dir =
        inverse(mat3(view_model)) *
        inverse(mat3(projection))
        * vec3(uv * 2.0 - vec2(1.0), 1.0);
    dir = normalize(dir);

    vec3 step_sign = sign(-dir);
    vec3 max_t = abs(((step_sign * 0.5 + vec3(0.5)) - (step_sign * iFS_volume_coords)) / dir);

    float depth = distance(iFS_camera_position * 0.5 + vec3(0.5), iFS_volume_coords);

    vec3 root_org = iFS_volume_coords
        - dir * 0.00001
        - (dir * min(depth, min(max_t.x, min(max_t.y, max_t.z))));

    vec3 org = root_org;

    vec3 normal = vec3(0.0);
    vec3 traversal_results = RegionTraversal(org, dir, normal);
    bool voxel_rendered = traversal_results.x > 0.0;
    float d = traversal_results.y;
    float t = traversal_results.z;

    if (!voxel_rendered)
    {
        color = vec4(vec3((d.x/16.0)), d.x/32.0);
        gl_FragDepth = 1.f;
    }
    else
    {
        vec3 hit = root_org+dir*t;
        vec3 sun_dir = normalize(vec3(-1.0, 2.0, 2.0));

        vec3 dummy = vec3(0.0);
        float shadow = 1.2 - RegionTraversal(hit + sun_dir*0.0001, sun_dir, dummy).x;

        float dist = distance(hit * 2.0 - vec3(1.0), iFS_camera_position);
        //float tone = (1.0 - (1.0 / (dist * 0.5 + 1.0))) * 2.0;
        float tone = 1.0 - exp(-dist*0.3);
        //color = vec4(shadow.xxx * max(0.0, dot(normal, sun_dir)) + tone * vec3(0.2, 0.5, 0.5), 1.0);
        color = vec4(mix(shadow.xxx * max(0.0, dot(normal, sun_dir)),
                         vec3(0.3, 0.5, 0.5),
                         tone),
                     1.0);

        gl_FragDepth = dist / 1000.f;
        //color = vec4(tone.xxx, 1.0);
    }
}

)"
