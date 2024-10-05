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
    numtk::mat4f Matrix() const;
};

struct state_t
{
    TurntableCamera camera;
    numtk::vec3<int32_t> cursor_coordinates = { 0, 0, 0 };

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

void ConstantResources(void* _pstate,
                       vktk::Context* _vulkan)
{
    state_t* _state = (state_t*)_pstate;

    {
        VkPushConstantRange push_constant_range = {};
        push_constant_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        push_constant_range.offset = 0;
        push_constant_range.size = sizeof(numtk::mat4f) * 2;

        VkPipelineLayoutCreateInfo pipeline_create{};
        pipeline_create.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipeline_create.pNext = nullptr;
        pipeline_create.flags = 0u;
        pipeline_create.setLayoutCount = 0u;
        pipeline_create.pSetLayouts = nullptr;
        pipeline_create.pushConstantRangeCount = 1;
        pipeline_create.pPushConstantRanges = &push_constant_range;
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

            void main() {
                vec2 grid_distance = abs(fract((iFS_texCoords * 16.0)) - vec2(0.5));

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
            _swapchain.render_pass,
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
                    vec3 view_main = abs(inverse(mat3(view_model)) * inverse(mat3(projection)) * vec3(0.0, 0.0, 1.0));
                    int index = 0;
                    if (view_main[index] < view_main[1])
                        index = 1;
                    if (view_main[index] < view_main[2])
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
            _swapchain.render_pass,
            {
                { cursor_vertex, VK_SHADER_STAGE_VERTEX_BIT },
                { cursor_fragment, VK_SHADER_STAGE_FRAGMENT_BIT }
            },
            {
                { vktk::DepthTestEnable, VK_FALSE },
                { vktk::DepthWriteEnable, VK_FALSE }
            },
            VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            {}
            );

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
    float how = (float)_swapchain.size.height / (float)_swapchain.size.width;

    vktk::CommandBuffer command_buffer = _vulkan->CreateCommandBuffer(_vulkan->present_queue);
    command_buffer.Begin();

    command_buffer.BeginRenderPass(_swapchain.size,
                                   _swapchain.framebuffers[_swapchain.index],
                                   _swapchain.render_pass,
                                   { { 0.13f, 0.18f, 0.24f, 0.f }, { 1.f, 0.f, 0.f, 0.f } });

    numtk::mat4f projection = numtk::mat4f::Perspective_VulkanNDC(0.1f, 100.f, 90.f*numtk::kDegrees, how);
    numtk::mat4f view = _state->camera.Matrix();

    {
        numtk::mat4f push_constants[2];
        push_constants[0] = projection;
        push_constants[1] = view;

        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _state->grid_pipeline);
        vkCmdPushConstants(command_buffer, _state->grid_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT,
                           0, sizeof(numtk::mat4f) * 2, (void*)push_constants);
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
            16.f };

        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _state->cursor_pipeline);
        vkCmdPushConstants(command_buffer, _state->grid_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT,
                           0, sizeof(CursorPushConstants), (void*)&push_constants);
        vkCmdDraw(command_buffer, 36u, 2u, 0u, 0u);
    }


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

    _state->camera.Update(_input, _past_input);

    numtk::vec3<int32_t> cursor_delta = { 0, 0, 0 };
    if (!_input->key_down['w'] && _past_input->key_down['w'])
        ++cursor_delta.y();
    if (!_input->key_down['s'] && _past_input->key_down['s'])
        --cursor_delta.y();
    if (!_input->key_down['d'] && _past_input->key_down['d'])
        ++cursor_delta.x();
    if (!_input->key_down['a'] && _past_input->key_down['a'])
        --cursor_delta.x();
    _state->cursor_coordinates += cursor_delta;

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

numtk::mat4f TurntableCamera::Matrix() const
{
    numtk::vec3f forward = {
        std::cos(spherical_coordinates[1]) * std::cos(spherical_coordinates[0]),
        std::sin(spherical_coordinates[1]),
        std::cos(spherical_coordinates[1]) * std::sin(spherical_coordinates[0])
    };
    forward = forward.normalise();

    numtk::vec3f up = { 0.f, 1.f, 0.f };
    if (std::abs(1.f - forward.dot(numtk::vec3f{ 0.f, 1.f, 0.f })) > 1e-3f)
        up = forward.cross(numtk::vec3f{ 0.f, 1.f, 0.f }).cross(forward).normalise();
    else
        up = forward.cross(numtk::vec3f{ 0.f, 0.f, 1.f }).cross(forward).normalise();

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
