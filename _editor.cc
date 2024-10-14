#include "_editor.hh"

#include <iostream>

#include "vktk.hh"
#include "numtk.hh"

namespace {

struct TurntableCamera
{
    numtk::vec3f spherical_coordinates = { 0.f, 0.f, 3.f };
    numtk::vec3f pivot = { 0.f, 0.f, 0.f };

    void Update(iotk::input_t const* _input,
                iotk::input_t const* _past_input);
    numtk::quatf Basis() const;
    numtk::mat4f View() const;
};

struct state_t
{
    static constexpr int32_t kGridSize = 16;
    static constexpr int32_t kGridVolume = kGridSize*kGridSize*kGridSize;
    TurntableCamera camera;
    numtk::vec3<int32_t> cursor_coordinates = { kGridSize/2, kGridSize/2, kGridSize/2 };

    VkRenderPass viewport_render_pass;
    VkRenderPass toolbar_render_pass;

    std::array<numtk::vec4<uint8_t>, kGridVolume> voxel_data = {};
    VkSampler sampler;
    vktk::Texture voxel_texture;
    vktk::DescriptorPool voxel_desc_pool;
    VkDescriptorSet voxel_desc_set;
    VkPipelineLayout voxel_pipeline_layout;
    VkPipeline voxel_pipeline;

    VkPipelineLayout grid_pipeline_layout;
    VkPipeline grid_pipeline;

    VkPipelineLayout cursor_pipeline_layout;
    VkPipeline cursor_pipeline;
};

struct CursorPushConstants
{
    numtk::mat4f projection;
    numtk::mat4f view_model;
    numtk::vec4f coordinates;
};

struct VoxelsPushConstants
{
    numtk::mat4f projection;
    numtk::mat4f view;
    numtk::vec2<int32_t> screen_size;
};

void ConstantResources(void* _pstate,
                       vktk::Context* _vulkan)
{
    state_t* _state = (state_t*)_pstate;

    {
        _state->voxel_texture = _vulkan->CreateTexture(
            state_t::kGridSize, state_t::kGridSize, state_t::kGridSize,
            VK_IMAGE_TYPE_3D, VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_SAMPLED_BIT,
            [_state](void* mapped_data) {
                numtk::vec4<uint8_t>* pixels = (numtk::vec4<uint8_t>*)mapped_data;
                for (uint32_t index = 0; index < state_t::kGridVolume; ++index) {
                    pixels[index] = _state->voxel_data[index];
                }
            });

        {
            vktk::CommandBuffer command_buffer = _vulkan->CreateCommandBuffer(_vulkan->present_queue);
            command_buffer.Begin();

            command_buffer.ImageTransitions(
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                {
                    {
                        _state->voxel_texture.image,
                        VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_NONE,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_IMAGE_ASPECT_COLOR_BIT,
                    }
                });

            command_buffer.End();
            command_buffer.Submit();
        }

        VkSamplerCreateInfo sampler_create{};
        sampler_create.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sampler_create.pNext = nullptr;
        sampler_create.flags = 0u;
        sampler_create.magFilter = VK_FILTER_NEAREST;
        sampler_create.minFilter = VK_FILTER_NEAREST;
        sampler_create.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampler_create.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_create.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_create.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_create.minLod = 0.0;
        sampler_create.maxLod = 0.0;
        CHECKCALL(vkCreateSampler, _vulkan->device,
                  &sampler_create, nullptr, &_state->sampler);

        _state->voxel_desc_pool = _vulkan->CreateDescriptorPool(1u, {
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0,
              VK_SHADER_STAGE_FRAGMENT_BIT, 1 }
        });

        VkDescriptorSetAllocateInfo descset_allocate{};
        descset_allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        descset_allocate.pNext= nullptr;
        descset_allocate.descriptorPool = _state->voxel_desc_pool.pool;
        descset_allocate.descriptorSetCount = 1u;
        descset_allocate.pSetLayouts = &_state->voxel_desc_pool.layout;
        CHECKCALL(vkAllocateDescriptorSets, _vulkan->device,
                  &descset_allocate, &_state->voxel_desc_set);

        {
            VkDescriptorImageInfo voxel_image_info{};
            voxel_image_info.sampler = _state->sampler;
            voxel_image_info.imageView = _state->voxel_texture.view;
            voxel_image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkWriteDescriptorSet desc_write = {};
            desc_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            desc_write.pNext = nullptr;
            desc_write.dstSet = _state->voxel_desc_set;
            desc_write.dstBinding = 0u;
            desc_write.dstArrayElement = 0u;
            desc_write.descriptorCount = 1u;
            desc_write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            desc_write.pImageInfo = &voxel_image_info;
            desc_write.pBufferInfo = nullptr;
            desc_write.pTexelBufferView = nullptr;

            vkUpdateDescriptorSets(_vulkan->device, 1u, &desc_write, 0u, nullptr);
        }

        VkPushConstantRange push_constant_range = {};
        push_constant_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        push_constant_range.offset = 0;
        push_constant_range.size = sizeof(numtk::mat4f) * 2 + sizeof(numtk::vec2<int32_t>);

        VkPipelineLayoutCreateInfo pipeline_create{};
        pipeline_create.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipeline_create.pNext = nullptr;
        pipeline_create.flags = 0u;
        pipeline_create.setLayoutCount = 1u;
        pipeline_create.pSetLayouts = &_state->voxel_desc_pool.layout;
        pipeline_create.pushConstantRangeCount = 1;
        pipeline_create.pPushConstantRanges = &push_constant_range;
        CHECKCALL(vkCreatePipelineLayout, _vulkan->device, &pipeline_create, nullptr,
                  &_state->voxel_pipeline_layout);
    }

    {
        VkPushConstantRange pc_ranges[2];
        {
            VkPushConstantRange& push_constant_range = pc_ranges[0];
            push_constant_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
            push_constant_range.offset = 0;
            push_constant_range.size = sizeof(numtk::mat4f) * 2;
        }

        {
            VkPushConstantRange& push_constant_range = pc_ranges[1];
            push_constant_range.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            push_constant_range.offset = sizeof(numtk::mat4f) * 2;
            push_constant_range.size = sizeof(float);
        }

        VkPipelineLayoutCreateInfo pipeline_create{};
        pipeline_create.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipeline_create.pNext = nullptr;
        pipeline_create.flags = 0u;
        pipeline_create.setLayoutCount = 0u;
        pipeline_create.pSetLayouts = nullptr;
        pipeline_create.pushConstantRangeCount = 2;
        pipeline_create.pPushConstantRanges = pc_ranges;
        CHECKCALL(vkCreatePipelineLayout, _vulkan->device, &pipeline_create, nullptr,
                  &_state->grid_pipeline_layout);
    }

    {
        VkPushConstantRange push_constant_range = {};
        push_constant_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        push_constant_range.offset = 0;
        push_constant_range.size = sizeof(CursorPushConstants);

        VkPipelineLayoutCreateInfo pipeline_create{};
        pipeline_create.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipeline_create.pNext = nullptr;
        pipeline_create.flags = 0u;
        pipeline_create.setLayoutCount = 0u;
        pipeline_create.pSetLayouts = nullptr;
        pipeline_create.pushConstantRangeCount = 1;
        pipeline_create.pPushConstantRanges = &push_constant_range;
        CHECKCALL(vkCreatePipelineLayout, _vulkan->device, &pipeline_create, nullptr,
                  &_state->cursor_pipeline_layout);
    }
}

void RenderResources(void* _pstate,
                     vktk::Context* _vulkan,
                     vktk::Swapchain const& _swapchain)
{
    state_t* _state = (state_t*)_pstate;

    {
        vktk::Context::RenderPassAttachment depth_attachment =
            { VK_FORMAT_D32_SFLOAT,
              VK_IMAGE_LAYOUT_UNDEFINED,
              VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

        _state->viewport_render_pass = _vulkan->CreateRenderPass(
            {
                { _swapchain.format,
                  VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL }
            },
            &depth_attachment
        );

        _state->toolbar_render_pass = _vulkan->CreateRenderPass(
            {
                { _swapchain.format,
                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                  VK_IMAGE_LAYOUT_PRESENT_SRC_KHR  }
            },
            nullptr
        );
    }

    {
        VkShaderModule voxel_vertex = _vulkan->CompileShader_GLSL(
            VK_SHADER_STAGE_VERTEX_BIT,
            "voxel_vertex",
            {
                "#version 450",
                "#define CW_CUBE",
#include "shaders/cube_vertices.vert.h"
                ,
                R"_(
                layout(push_constant, std430) uniform PushConstant {
                    mat4 projection;
                    mat4 view;
                    ivec2 screen_size;
                };
                layout(location = 0) out vec3 iFS_volume_coords;
                layout(location = 1) out vec3 iFS_camera_position;

                void main() {
                    vec3 vertex = CurrentVertex();
                    gl_Position = projection * view * vec4(vertex, 1.0);

                    iFS_camera_position = (inverse(view) * vec4(vec3(0.0), 1.0)).xyz;
                    iFS_volume_coords = vertex * 0.5 + vec3(0.5);
                }
                )_"
            });

        VkShaderModule voxel_fragment = _vulkan->CompileShader_GLSL(
            VK_SHADER_STAGE_FRAGMENT_BIT,
            "voxel_fragment",
            {
                "#version 450",
                #include "shaders/voxel_traversal.h"
                ,
                R"_(
                layout(location = 0) in vec3 iFS_volume_coords;
                layout(location = 1) in vec3 iFS_camera_position;
                layout(location = 0) out vec4 oFS_color;

                layout(set = 0, binding = 0) uniform sampler3D voxels;

                layout(push_constant, std430) uniform PushConstant {
                    mat4 projection;
                    mat4 view;
                    ivec2 screen_size;
                };

                #define GRID_SIZE 16.0

                float VoxelsTraversal(vec3 org, vec3 dir, out vec3 color, out vec3 normal, float depth)
                {
                    DDAContext ctx = InitDDAContext(org, dir, GRID_SIZE);
                    while (ctx.mask.w == 1.0)
                    {
                        vec4 voxel = texture(voxels, ctx.voxel_coords);
                        if (voxel.w != 0.0)
                        {
                            color = voxel.xyz;
                            normal = ctx.mask.xyz * -ctx.step_sign;
                            return ctx.t;
                        }
                        StepDDAContext(ctx);
                    }

                    return -1.0/0.0;
                }

                void main() {
                    vec2 uv = vec2(gl_FragCoord.xy) / vec2(screen_size.xy);
                    mat3 inv_viewproj =
                        inverse(mat3(view))
                        * inverse(mat3(projection));
                    vec3 dir = normalize(
                        inv_viewproj
                        * vec3(uv * 2.0 - vec2(1.0), 1.0));
                    vec3 cam_forward = normalize(
                        inv_viewproj
                        * vec3(0.0, 0.0, 1.0));

                    vec3 step_sign = sign(-dir);
                    vec3 max_t = abs(((step_sign * 0.5 + vec3(0.5)) - (step_sign * iFS_volume_coords)) / dir);

                    float depth = distance(iFS_camera_position * 0.5 + vec3(0.5), iFS_volume_coords);
                    float region_begin = min(max_t.x, min(max_t.y, max_t.z));

                    vec3 root_org = iFS_volume_coords
                        - dir * 0.00001
                        - (dir * min(depth, region_begin));

                    depth = max(depth - region_begin, 0.0);

                    vec3 org = root_org;

                    vec3 normal = vec3(0.0);
                    vec3 color = vec3(0.0);
                    float t_hit = VoxelsTraversal(org, dir, color, normal, depth);
                    if (t_hit >= 0.0)
                    {
                        float diffuse = min(1.0, 0.2 + max(0.0, dot(normal, -cam_forward)));
                        oFS_color = vec4(color * diffuse, 1.0);
                        vec3 hit_p = (org + dir * t_hit) * 2.0 - vec3(1.0);
                        vec4 cs_p = projection * view * vec4(hit_p, 1.0);
                        gl_FragDepth = cs_p.z / cs_p.w;
                    }
                    else
                        discard;
                }
                )_"
            });

        _state->voxel_pipeline = _vulkan->CreatePipeline(
            _state->voxel_pipeline_layout,
            _state->viewport_render_pass,
            {
                { voxel_vertex, VK_SHADER_STAGE_VERTEX_BIT },
                { voxel_fragment, VK_SHADER_STAGE_FRAGMENT_BIT }
            },
            VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, {});

        vkDestroyShaderModule(_vulkan->device, voxel_vertex, nullptr);
        vkDestroyShaderModule(_vulkan->device, voxel_fragment, nullptr);
    }

    {
        VkShaderModule grid_vertex = _vulkan->CompileShader_GLSL(
            VK_SHADER_STAGE_VERTEX_BIT,
            "grid_vertex",
            {
                "#version 450",
                "#define CW_CUBE",
#include "shaders/cube_vertices.vert.h"
                ,
                R"_(
            layout(push_constant, std430) uniform PushConstants {
                mat4 projection;
                mat4 view_model;
            };

            layout(location = 0) out vec2 iFS_texCoords;
            void main() {
                gl_Position = projection * view_model * vec4(CurrentVertex(), 1.0);
                iFS_texCoords = CurrentUV();
            }
            )_"
            });

        VkShaderModule grid_fragment = _vulkan->CompileShader_GLSL(
            VK_SHADER_STAGE_FRAGMENT_BIT,
            "grid_fragment",
            {
                "#version 450",
                R"_(
            layout(location = 0) in vec2 iFS_texCoords;
            layout(location = 0) out vec4 oFS_color;

            layout(push_constant, std430) uniform PushConstants{
                layout(offset = 128) float grid_size;
            };

            void main() {
                vec2 grid_distance = abs(fract((iFS_texCoords * grid_size)) - vec2(0.5));

                vec2 grid_value_0_split = max((grid_distance - 0.45), vec2(0.0)) * 10.0;
                float grid_value_0 = min(dot(pow(grid_value_0_split, vec2(0.7)), vec2(1.0)), 1.0);

                vec2 grid_value_1_split = min(max(((grid_distance - 0.08) * 10.0), 0.0), vec2(3.0)) / 3.0;
                float grid_value_1 = min(dot(grid_value_1_split, vec2(1.0)), 1.0) * 0.05;

                float grid_value = min(grid_value_0 + grid_value_1, 1.0);

                vec3 grid_color = vec3(0.42, 0.79, 1.0) * 1.2;
                vec3 bg_color = vec3(0.13, 0.18, 0.24);

                oFS_color = vec4(mix(bg_color, grid_color, grid_value), 1.0);
            }
            )_"
            });

        _state->grid_pipeline = _vulkan->CreatePipeline(
            _state->grid_pipeline_layout,
            _state->viewport_render_pass,
            {
                { grid_vertex, VK_SHADER_STAGE_VERTEX_BIT },
                { grid_fragment, VK_SHADER_STAGE_FRAGMENT_BIT }
            },
            VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            {}
            );

        vkDestroyShaderModule(_vulkan->device, grid_vertex, nullptr);
        vkDestroyShaderModule(_vulkan->device, grid_fragment, nullptr);
    }

    {
        VkShaderModule cursor_vertex = _vulkan->CompileShader_GLSL(
            VK_SHADER_STAGE_VERTEX_BIT,
            "cursor_vertex",
            {
                "#version 450",
#include "shaders/cube_vertices.vert.h"
                ,
                R"_(
                layout(push_constant, std430) uniform PushConstants {
                    mat4 projection;
                    mat4 view_model;
                    vec4 coordinates;
                };

                void main() {
                    vec3 view_dir = abs(inverse(mat3(view_model)) * inverse(mat3(projection)) * vec3(0.0, 0.0, 1.0));
                    int index = 0;
                    if (view_dir[index] < view_dir[1])
                        index = 1;
                    if (view_dir[index] < view_dir[2])
                        index = 2;

                    vec3 selection = vec3(1.0);
                    selection[(index + 1 + gl_InstanceIndex) % 3] = 0.0;
                    vec3 inverse_selection = vec3(1.0) - selection;

                    vec3 vertex = CurrentVertex();
                    vertex = vertex * 0.5 + vec3(0.5);

                    vertex /= (coordinates.www / (inverse_selection * coordinates.w + selection));
                    vertex += (coordinates.xyz / coordinates.w) * selection;
                    vertex = vertex * 2.0 - vec3(1.0);
                    gl_Position = projection * view_model * vec4(vertex, 1.0);
                }
                )_"
            });

        VkShaderModule cursor_fragment = _vulkan->CompileShader_GLSL(
            VK_SHADER_STAGE_FRAGMENT_BIT,
            "cursor_fragment",
            {
                "#version 450",
                R"_(
                layout(location = 0) out vec4 oFS_color;
                void main() {
                    oFS_color = vec4(1.0, 1.0, 0.0, 0.5);
                }
                )_"
            });

        _state->cursor_pipeline = _vulkan->CreatePipeline(
            _state->cursor_pipeline_layout,
            _state->viewport_render_pass,
            {
                { cursor_vertex, VK_SHADER_STAGE_VERTEX_BIT },
                { cursor_fragment, VK_SHADER_STAGE_FRAGMENT_BIT }
            },
            {
                { vktk::DepthTestEnable, VK_TRUE },
                { vktk::DepthWriteEnable, VK_FALSE }
            },
            VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, {} );

        vkDestroyShaderModule(_vulkan->device, cursor_vertex, nullptr);
        vkDestroyShaderModule(_vulkan->device, cursor_fragment, nullptr);
    }
}

void Draw(void* _pstate,
          vktk::Context* _vulkan,
          vktk::Swapchain const& _swapchain,
          VkSemaphore _acquire_semaphore)
{
    state_t* _state = (state_t*)_pstate;

    numtk::bounds2i editor_viewport{
        { 0, 0 },
        { ((int32_t)_swapchain.size.width * 2) / 3, (int32_t)_swapchain.size.height }
    };
    numtk::bounds2i toolbar_viewport{
        { ((int32_t)_swapchain.size.width * 2) / 3, 0 },
        { (int32_t)_swapchain.size.width / 3, (int32_t)_swapchain.size.height }
    };

    float how = (float)editor_viewport.extent.y() / (float)editor_viewport.extent.x();

    vktk::CommandBuffer command_buffer = _vulkan->CreateCommandBuffer(_vulkan->present_queue);
    command_buffer.Begin();

    command_buffer.ImageTransitions(
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        {
            {
                _state->voxel_texture.image,
                VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_ASPECT_COLOR_BIT,
            }
        });

    command_buffer.UpdateTexture(
        _state->voxel_texture,
        VkOffset3D{ 0, 0, 0 }, VkExtent3D{ state_t::kGridSize, state_t::kGridSize, state_t::kGridSize },
        [_state](void* mapped_data) {
            numtk::vec4<uint8_t>* pixels = (numtk::vec4<uint8_t>*)mapped_data;
            for (uint32_t index = 0; index < state_t::kGridVolume; ++index) {
                pixels[index] = _state->voxel_data[index];
            }
        });

    command_buffer.ImageTransitions(
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        {
            {
                _state->voxel_texture.image,
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_IMAGE_ASPECT_COLOR_BIT,
            }
        });

    command_buffer.BeginRenderPass(editor_viewport,
                                   _swapchain.framebuffers[_swapchain.index],
                                   _state->viewport_render_pass,
                                   { { 0.13f, 0.18f, 0.24f, 0.f }, { 1.f, 0.f, 0.f, 0.f } });

    numtk::mat4f projection = numtk::mat4f::Perspective_VulkanNDC(0.1f, 100.f, 90.f*numtk::kDegrees, how);
    numtk::mat4f view = _state->camera.View();

    {
        VoxelsPushConstants pc = {};
        pc.projection = projection;
        pc.view = view;
        pc.screen_size = {
            (int32_t)editor_viewport.extent.x(),
            (int32_t)editor_viewport.extent.y()
        };

        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _state->voxel_pipeline);
        vkCmdBindDescriptorSets(command_buffer,
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                _state->voxel_pipeline_layout, 0u,
                                1u, &_state->voxel_desc_set,
                                0u, nullptr);
        vkCmdPushConstants(command_buffer, _state->voxel_pipeline_layout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(VoxelsPushConstants), (void*)&pc);
        vkCmdDraw(command_buffer, 36u, 1u, 0u, 0u);
    }

    {
        numtk::mat4f pc_matrices[2];
        pc_matrices[0] = projection;
        pc_matrices[1] = view;

        float pc_grid_size = (float)state_t::kGridSize;

        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _state->grid_pipeline);
        vkCmdPushConstants(command_buffer, _state->grid_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT,
                           0, sizeof(numtk::mat4f) * 2, (void*)pc_matrices);
        vkCmdPushConstants(command_buffer, _state->grid_pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT,
                           sizeof(numtk::mat4f) * 2, sizeof(float), (void*)&pc_grid_size);
        vkCmdDraw(command_buffer, 36u, 1u, 0u, 0u);
    }

    {
        CursorPushConstants push_constants = {};
        push_constants.projection = projection;
        push_constants.view_model = view;
        push_constants.coordinates = numtk::vec4f{
            (float)_state->cursor_coordinates.x(),
            (float)_state->cursor_coordinates.y(),
            (float)_state->cursor_coordinates.z(),
            (float)state_t::kGridSize };

        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _state->cursor_pipeline);
        vkCmdPushConstants(command_buffer, _state->grid_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT,
                           0, sizeof(CursorPushConstants), (void*)&push_constants);
        vkCmdDraw(command_buffer, 36u, 2u, 0u, 0u);
    }

    vkCmdEndRenderPass(command_buffer);

    command_buffer.BeginRenderPass(toolbar_viewport,
                                   _swapchain.framebuffers[_swapchain.index],
                                   _state->toolbar_render_pass,
                                   { { 0.3f, 0.3f, 0.3f, 0.f }, { 1.f, 0.f, 0.f, 0.f } });

    vkCmdEndRenderPass(command_buffer);

    command_buffer.End();

    {
        VkPipelineStageFlags wait_dst_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        command_buffer.Submit(nullptr, &_acquire_semaphore, &wait_dst_stage, 1u);
    }

    {
        VkPresentInfoKHR present_info{};
        present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present_info.pNext = nullptr;
        present_info.waitSemaphoreCount = 0u;
        present_info.pWaitSemaphores = nullptr;
        present_info.swapchainCount = 1;
        present_info.pSwapchains = &_swapchain.handle;
        present_info.pImageIndices = &_swapchain.index;
        present_info.pResults = nullptr;
        CHECKCALL(_vulkan->fp.QueuePresentKHR,
                  _vulkan->present_queue, &present_info);
    }
}

bool Update(void* _pstate,
            vktk::Context*,
            iotk::input_t const* _input,
            iotk::input_t const* _past_input)
{
    state_t* _state = (state_t*)_pstate;

    numtk::quatf camera_basis = _state->camera.Basis();
    numtk::vec3f camera_left = camera_basis * numtk::vec3f{ 1.f, 0.f, 0.f };
    numtk::vec3f camera_up = camera_basis * numtk::vec3f{ 0.f, 1.f, 0.f };

    numtk::vec3<int32_t> cursor_left = {};
    {
        int index = 0;
        if (std::abs(camera_left[index]) < std::abs(camera_left[1]))
            index = 1;
        if (std::abs(camera_left[index]) < std::abs(camera_left[2]))
            index = 2;
        cursor_left[index] = camera_left[index] < 0.f ? -1 : 1;
    }

    numtk::vec3<int32_t> cursor_up = {};
    {
        int index = 0;
        if (std::abs(camera_up[index]) < std::abs(camera_up[1]))
            index = 1;
        if (std::abs(camera_up[index]) < std::abs(camera_up[2]))
            index = 2;
        cursor_up[index] = camera_up[index] < 0.f ? -1 : 1;
    }

    numtk::vec3<int32_t> cursor_forward = cursor_left.cross(cursor_up);

    numtk::vec3<int32_t> cursor_delta = { 0, 0, 0 };
    if (!_input->key_down['w'] && _past_input->key_down['w'])
        cursor_delta += cursor_up;
    if (!_input->key_down['s'] && _past_input->key_down['s'])
        cursor_delta -= cursor_up;
    if (!_input->key_down['a'] && _past_input->key_down['a'])
        cursor_delta += cursor_left;
    if (!_input->key_down['d'] && _past_input->key_down['d'])
        cursor_delta -= cursor_left;
    if (!_input->key_down['r'] && _past_input->key_down['r'])
        cursor_delta += cursor_forward;
    if (!_input->key_down['f'] && _past_input->key_down['f'])
        cursor_delta -= cursor_forward;

    _state->cursor_coordinates += cursor_delta;
    _state->cursor_coordinates.x() = std::min(std::max(_state->cursor_coordinates.x(), 0), state_t::kGridSize-1);
    _state->cursor_coordinates.y() = std::min(std::max(_state->cursor_coordinates.y(), 0), state_t::kGridSize-1);
    _state->cursor_coordinates.z() = std::min(std::max(_state->cursor_coordinates.z(), 0), state_t::kGridSize-1);

    if (!_input->key_down[' '] && _past_input->key_down[' '])
    {
        numtk::vec3<int32_t>& point = _state->cursor_coordinates;
        uint32_t voxel_index = point.x()
            + point.y()*state_t::kGridSize
            + point.z()*state_t::kGridSize*state_t::kGridSize;
        if (_state->voxel_data[voxel_index].w() == 0)
            _state->voxel_data[voxel_index] = numtk::vec4<uint8_t>{ 0, 255, 255, 255 };
        else
            _state->voxel_data[voxel_index] = numtk::vec4<uint8_t>{ 0, 0, 0, 0 };
    }

    _state->camera.Update(_input, _past_input);

    return true;
}

void TurntableCamera::Update(iotk::input_t const* _input,
                             iotk::input_t const* _past_input)
{
    if (_input->button_down & iotk::fMouseButton::kLeftBtn)
    {
        numtk::vec2<int32_t> last_position{ _past_input->cursor[0], _past_input->cursor[1] };
        numtk::vec2<int32_t> position{ _input->cursor[0], _input->cursor[1] };
        numtk::vec2<int32_t> delta = position - last_position;
        spherical_coordinates += numtk::vec3f{
            0.01f * (float)delta.x(),
            0.01f * (float)delta.y(),
            0.f
        };

        spherical_coordinates.y() =
            std::min(std::max(spherical_coordinates.y(), -numtk::kPi*0.4999f), numtk::kPi*0.4999f);
    }

    {
        spherical_coordinates.z() *= (1.f + 0.0005f * (float)_input->wheel_delta);
        if (spherical_coordinates.z() < 0.01f)
            spherical_coordinates.z() = 0.01f;
    }
}

numtk::quatf TurntableCamera::Basis() const
{
    return
        numtk::quatf::AngleAxis(spherical_coordinates[0], { 0.f, -1.f, 0.f })
        * numtk::quatf::AngleAxis(spherical_coordinates[1], { 1.f, 0.f, 0.f });
}

numtk::mat4f TurntableCamera::View() const
{
    numtk::quatf basis = Basis();
    numtk::vec3f forward = basis * numtk::vec3f{ 0.f, 0.f, -1.f };
    numtk::vec3f up = basis * numtk::vec3f{ 0.f, 1.f, 0.f };
    numtk::vec3f center = pivot;

    return
        numtk::mat4f::View({ 0.f, 0.f, -1.f },
                           { 0.f, 1.f, 0.f },
                           { 0.f, 0.f, spherical_coordinates.z() })
        * numtk::mat4f::View(forward, up, center);
}

}

namespace Editor
{

Interface::Interface()
    : state{ new state_t{} }
{ ReloadPointers(); }

Interface::~Interface()
{
    delete (state_t*)state;
}

void Interface::ReloadPointers()
{
    ConstantResources = ::ConstantResources;
    RenderResources = ::RenderResources;
    Draw = ::Draw;
    Update = ::Update;
    Shutdown = nullptr;
}

}
