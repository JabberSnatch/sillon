#include "_editor.hh"

#include <iostream>

#include "vktk.hh"

namespace {

struct state_t
{
    VkPipelineLayout grid_pipeline_layout;
    VkPipeline grid_pipeline;
};

void ConstantResources(void* _pstate,
                       vktk::Context* _vulkan)
{
    state_t* _state = (state_t*)_pstate;

    VkPipelineLayoutCreateInfo pipeline_create{};
    pipeline_create.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_create.pNext = nullptr;
    pipeline_create.flags = 0u;
    pipeline_create.setLayoutCount = 0u;
    pipeline_create.pSetLayouts = nullptr;
    pipeline_create.pushConstantRangeCount = 0;
    pipeline_create.pPushConstantRanges = nullptr;
    CHECKCALL(vkCreatePipelineLayout, _vulkan->device, &pipeline_create, nullptr,
              &_state->grid_pipeline_layout);
}

void RenderResources(void* _pstate,
                     vktk::Context* _vulkan,
                     vktk::Swapchain const& _swapchain)
{
    state_t* _state = (state_t*)_pstate;

    VkShaderModule grid_vertex = _vulkan->CompileShader_GLSL(
        VK_SHADER_STAGE_VERTEX_BIT,
        "grid_vertex",
        {
            "#version 450",
            "#define CW_CUBE",
            #include "shaders/cube_vertices.vert.h"
            ,
            R"_(
            void main() {
                gl_Position = vec4(CurrentVertex(), 1.0);
            }
            )_"
        });

    VkShaderModule grid_fragment = _vulkan->CompileShader_GLSL(
        VK_SHADER_STAGE_FRAGMENT_BIT,
        "grid_fragment",
        {
            "#version 450",
            R"_(
            layout(location = 0) out vec4 oFS_color;
            void main() {
                oFS_color = vec4(1.0, 1.0, 0.0, 1.0);
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

void Draw(void* _pstate,
          vktk::Context* _vulkan,
          vktk::Swapchain const& _swapchain,
          VkSemaphore _acquire_semaphore)
{
    state_t* _state = (state_t*)_pstate;

    vktk::CommandBuffer command_buffer = _vulkan->CreateCommandBuffer(_vulkan->present_queue);
    command_buffer.Begin();

    command_buffer.BeginRenderPass(_swapchain.size,
                                   _swapchain.framebuffers[_swapchain.index],
                                   _swapchain.render_pass,
                                   { { 0.f, 1.f, 1.f, 0.f }, { 1.f, 0.f, 0.f, 0.f } });

    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _state->grid_pipeline);
    vkCmdDraw(command_buffer, 36u, 1u, 0u, 0u);
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

bool Update(void*, vktk::Context*, iotk::input_t const*, iotk::input_t const*)
{
    return true;
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
    Update = nullptr;
    Shutdown = nullptr;
}

}
