#include "_icosphere.hh"

#include "loader/iotk.hpp"
#include "numtk.hh"
#include "vktk.hh"

#include <iostream>

namespace {

struct RenderSemaphore {
    VkSemaphore handle;
    VkFence fence;
};

struct state_t
{
    VkDescriptorSetLayout descset_layout;
    VkPipelineLayout pipeline_layout;
    VkPipeline graphics_pipeline = VK_NULL_HANDLE;
    vktk::Buffer index_buffer;
    vktk::Buffer vertex_buffer;
    vktk::Buffer matrix_buffer;
    vktk::Buffer staging_buffer;
    void* staging_area;

    VkSemaphore copy_semaphore;
    VkSemaphore upload_semaphore = VK_NULL_HANDLE;
    std::vector<RenderSemaphore> render_semaphores;

    VkDescriptorPool descriptor_pool;
    VkDescriptorSet descriptor_set;

    std::vector<VkSemaphore> dead_semaphores;
};

void ConstantResources(void* _pstate,
                       vktk::Context* _vulkan)
{
    state_t* _state = (state_t*)_pstate;

    VkDescriptorPoolSize descpool_sizes[1] = {
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 },
    };

    VkDescriptorPoolCreateInfo descpool_create = {};
    descpool_create.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descpool_create.pNext = nullptr;
    descpool_create.flags = 0u;
    descpool_create.maxSets = 1u;
    descpool_create.poolSizeCount = sizeof(descpool_sizes) / sizeof(descpool_sizes[0]);
    descpool_create.pPoolSizes = descpool_sizes;
    CHECKCALL(vkCreateDescriptorPool, _vulkan->device,
              &descpool_create, nullptr, &_state->descriptor_pool);

    VkDescriptorSetLayoutBinding bindings[1];
    {
        VkDescriptorSetLayoutBinding& binding = bindings[0];
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_ALL;
        binding.pImmutableSamplers = nullptr;
    }

    VkDescriptorSetLayoutCreateInfo descset_create = {};
    descset_create.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descset_create.pNext = nullptr;
    descset_create.flags = 0;
    descset_create.bindingCount = 1;
    descset_create.pBindings = bindings;
    CHECKCALL(vkCreateDescriptorSetLayout, _vulkan->device, &descset_create, nullptr,
              &_state->descset_layout);

    VkDescriptorSetAllocateInfo descset_allocate{};
    descset_allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    descset_allocate.pNext= nullptr;
    descset_allocate.descriptorPool = _state->descriptor_pool;
    descset_allocate.descriptorSetCount = 1u;
    descset_allocate.pSetLayouts = &_state->descset_layout;
    CHECKCALL(vkAllocateDescriptorSets, _vulkan->device,
              &descset_allocate, &_state->descriptor_set);

    VkPipelineLayoutCreateInfo pipeline_create{};
    pipeline_create.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_create.pNext = nullptr;
    pipeline_create.flags = 0u;
    pipeline_create.setLayoutCount = 1;
    pipeline_create.pSetLayouts = &_state->descset_layout;
    pipeline_create.pushConstantRangeCount = 0;
    pipeline_create.pPushConstantRanges = nullptr;
    CHECKCALL(vkCreatePipelineLayout, _vulkan->device, &pipeline_create, nullptr,
              &_state->pipeline_layout);

    _state->staging_buffer = _vulkan->CreateBuffer(
        16 * 1024 * 1024,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
        | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    _state->index_buffer = _vulkan->CreateBuffer(
        20 * sizeof(uint32_t) * 3,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT
        | VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

    struct VertexData {
        numtk::vec3f position;
        numtk::vec3f normal;
    };

    _state->vertex_buffer = _vulkan->CreateBuffer(
        12 * sizeof(VertexData),
        VK_BUFFER_USAGE_TRANSFER_DST_BIT
        | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

    _state->matrix_buffer = _vulkan->CreateBuffer(
        16 * sizeof(float) * 100,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT
        | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);

    _state->staging_area = nullptr;
    CHECKCALL(vkMapMemory,
              _vulkan->device, _state->staging_buffer.memory,
              0, VK_WHOLE_SIZE, 0, &_state->staging_area);

    VertexData* vertices = (VertexData*)_state->staging_area;
    numtk::vec3u* triangles = (numtk::vec3u*)(vertices+12);

    numtk::mat4f* matrices = (numtk::mat4f*)(triangles+20);
    {
        matrices[0] =
            numtk::mat4f::Perspective(0.1f, 100.f, 90.f*numtk::kDegrees, 1.f)
            * numtk::mat4f::View({ 0.f, 0.f, 1.f }, { 0.f, 1.f, 0.f }, { 0.f, 0.f, -1.7f });
    }

    {
        static const auto spherical_to_cartesian = [](numtk::vec3f const& v){
            // v = { lat, long, elevation }
            // { 0, 0, 1 } -> { 0, 0, 1 }
            // { 90, 0, 1 } -> { 0, 1, 0 }
            // { -90, 0, 1 } -> { 0, -1, 0 }
            return v.z() * numtk::vec3f{
                std::cos(v.y())*std::cos(v.x()),
                std::sin(v.x()),
                std::sin(v.y())*std::cos(v.x())
            };
        };

        static constexpr float kPoleLat = 90.f*numtk::kDegrees;
        static constexpr float kCommonLat = 26.57f*numtk::kDegrees;
        static constexpr float kLongIncrement = 36.f*numtk::kDegrees;
        vertices[0].position = spherical_to_cartesian({ kPoleLat, 0.f, 1.f });
        vertices[1].position = spherical_to_cartesian({ kCommonLat, kLongIncrement*0.f, 1.f });
        vertices[2].position = spherical_to_cartesian({ -kCommonLat, kLongIncrement*1.f, 1.f });
        vertices[3].position = spherical_to_cartesian({ kCommonLat, kLongIncrement*2.f, 1.f });
        vertices[4].position = spherical_to_cartesian({ -kCommonLat, kLongIncrement*3.f, 1.f });
        vertices[5].position = spherical_to_cartesian({ kCommonLat, kLongIncrement*4.f, 1.f });
        vertices[6].position = spherical_to_cartesian({ -kCommonLat, kLongIncrement*5.f, 1.f });
        vertices[7].position = spherical_to_cartesian({ kCommonLat, kLongIncrement*6.f, 1.f });
        vertices[8].position = spherical_to_cartesian({ -kCommonLat, kLongIncrement*7.f, 1.f });
        vertices[9].position = spherical_to_cartesian({ kCommonLat, kLongIncrement*8.f, 1.f });
        vertices[10].position = spherical_to_cartesian({ -kCommonLat, kLongIncrement*9.f, 1.f });
        vertices[11].position = spherical_to_cartesian({ -kPoleLat, 0.f, 1.f });

        vertices[0].normal = vertices[0].position.normalise();
        vertices[1].normal = vertices[1].position.normalise();
        vertices[2].normal = vertices[2].position.normalise();
        vertices[3].normal = vertices[3].position.normalise();
        vertices[4].normal = vertices[4].position.normalise();
        vertices[5].normal = vertices[5].position.normalise();
        vertices[6].normal = vertices[6].position.normalise();
        vertices[7].normal = vertices[7].position.normalise();
        vertices[8].normal = vertices[8].position.normalise();
        vertices[9].normal = vertices[9].position.normalise();
        vertices[10].normal = vertices[10].position.normalise();
        vertices[11].normal = vertices[11].position.normalise();
    }

    {
        triangles[0] = { 0, 1, 3 };
        triangles[1] = { 0, 3, 5 };
        triangles[2] = { 0, 5, 7 };
        triangles[3] = { 0, 7, 9 };
        triangles[4] = { 0, 9, 1 };

        triangles[5] = { 11, 10, 8 };
        triangles[6] = { 11, 8, 6 };
        triangles[7] = { 11, 6, 4 };
        triangles[8] = { 11, 4, 2 };
        triangles[9] = { 11, 2, 10 };

        triangles[10] = { 1, 2, 3 };
        triangles[11] = { 3, 4, 5 };
        triangles[12] = { 5, 6, 7 };
        triangles[13] = { 7, 8, 9 };
        triangles[14] = { 9, 10, 1 };

        triangles[15] = { 10, 9, 8 };
        triangles[16] = { 8, 7, 6 };
        triangles[17] = { 6, 5, 4 };
        triangles[18] = { 4, 3, 2 };
        triangles[19] = { 2, 1, 10 };
    }

    vktk::CommandBuffer copy_commands = _vulkan->CreateCommandBuffer(_vulkan->copy_queue);
    vktk::CommandBuffer present_commands = _vulkan->CreateCommandBuffer(_vulkan->present_queue);

    copy_commands.Begin();
    present_commands.Begin();

    copy_commands.BufferTransitions(
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        {
            { _state->vertex_buffer,
              VK_ACCESS_NONE,
              VK_ACCESS_TRANSFER_WRITE_BIT },
            { _state->index_buffer,
              VK_ACCESS_NONE,
              VK_ACCESS_TRANSFER_WRITE_BIT },
            { _state->matrix_buffer,
              VK_ACCESS_NONE,
              VK_ACCESS_TRANSFER_WRITE_BIT },
        });

    uint64_t offset = 0;
    copy_commands.BufferCopy(_state->vertex_buffer, 0,
                             _state->staging_buffer, offset,
                             sizeof(VertexData)*12);
    offset += sizeof(VertexData)*12;
    copy_commands.BufferCopy(_state->index_buffer, 0,
                             _state->staging_buffer, offset,
                             sizeof(numtk::vec3u)*20);
    offset += sizeof(numtk::vec3u)*20;
    copy_commands.BufferCopy(_state->matrix_buffer, 0,
                             _state->staging_buffer, offset,
                             sizeof(numtk::mat4f));

    copy_commands.BufferTransitions(
        present_commands,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
        {
            { _state->vertex_buffer,
              VK_ACCESS_TRANSFER_WRITE_BIT,
              VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT },
            { _state->index_buffer,
              VK_ACCESS_TRANSFER_WRITE_BIT,
              VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT },
            { _state->matrix_buffer,
              VK_ACCESS_TRANSFER_WRITE_BIT,
              VK_ACCESS_UNIFORM_READ_BIT }
        });

    copy_commands.End();
    present_commands.End();

    {
        VkSemaphoreCreateInfo semaphore_info = {};
        semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        semaphore_info.pNext = nullptr;
        semaphore_info.flags = 0;
        CHECKCALL(vkCreateSemaphore,
                  _vulkan->device, &semaphore_info, nullptr, &_state->copy_semaphore);
        CHECKCALL(vkCreateSemaphore,
                  _vulkan->device, &semaphore_info, nullptr, &_state->upload_semaphore);
    }

    {
        VkSubmitInfo submit_info{};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.pNext = nullptr;
        submit_info.waitSemaphoreCount = 0u;
        submit_info.pWaitSemaphores = nullptr;
        submit_info.pWaitDstStageMask = nullptr;
        submit_info.commandBufferCount = 1u;
        submit_info.pCommandBuffers = &copy_commands.handle;
        submit_info.signalSemaphoreCount = 1u;
        submit_info.pSignalSemaphores = &_state->copy_semaphore;
        vkQueueSubmit(_vulkan->copy_queue, 1, &submit_info, VK_NULL_HANDLE);
    }

    {
        VkPipelineStageFlags dst_stage_mask =
            VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;

        VkSubmitInfo submit_info{};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.pNext = nullptr;
        submit_info.waitSemaphoreCount = 1u;
        submit_info.pWaitSemaphores = &_state->copy_semaphore;
        submit_info.pWaitDstStageMask = &dst_stage_mask;
        submit_info.commandBufferCount = 1u;
        submit_info.pCommandBuffers = &present_commands.handle;
        submit_info.signalSemaphoreCount = 1u;
        submit_info.pSignalSemaphores = &_state->upload_semaphore;
        vkQueueSubmit(_vulkan->present_queue, 1, &submit_info, VK_NULL_HANDLE);
    }
}

void RenderResources(void* _pstate,
                     vktk::Context* _vulkan,
                     vktk::Swapchain const& _swapchain)
{
    state_t* _state = (state_t*)_pstate;

    static char const vshader[] = R"_(
    #version 450
    layout(location = 0) in vec3 iVS_position;
    layout(location = 1) in vec3 iVS_normal;
    layout(location = 0) out vec3 oVS_normal;
    layout(std140, set = 0, binding = 0) uniform MatrixBlock {
        mat4 view_projection;
    };
    void main() {
        gl_Position = view_projection * vec4(iVS_position, 1.0);
        oVS_normal = iVS_normal;
    }
    )_";
    static uint32_t const vsize = sizeof(vshader) - 1u;

    static char const fshader[] = R"_(
    #version 450
    layout(location = 0) in vec3 iFS_normal;
    layout(location = 0) out vec4 oFS_color;
    void main() {
        oFS_color = vec4(normalize(iFS_normal) * 0.5 + 0.5, 1.0);
    }
    )_";
    static uint32_t const fsize = sizeof(fshader) - 1u;

    VkShaderModule vmodule =
        _vulkan->CompileShader_GLSL(VK_SHADER_STAGE_VERTEX_BIT,
                                    "vshader",
                                    vshader,
                                    vsize);

    VkShaderModule fmodule =
        _vulkan->CompileShader_GLSL(VK_SHADER_STAGE_FRAGMENT_BIT,
                                    "fshader",
                                    fshader,
                                    fsize);

    if (_state->graphics_pipeline != VK_NULL_HANDLE)
        vkDestroyPipeline(_vulkan->device, _state->graphics_pipeline, nullptr);

    _state->graphics_pipeline = _vulkan->CreatePipeline(
        _state->pipeline_layout,
        _swapchain.render_pass,
        {
            { vmodule, VK_SHADER_STAGE_VERTEX_BIT },
            { fmodule, VK_SHADER_STAGE_FRAGMENT_BIT },
        },
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        {
            { 0, 0, VK_VERTEX_INPUT_RATE_VERTEX, VK_FORMAT_R32G32B32_SFLOAT,
              0, sizeof(float)*6 },
            { 1, 1, VK_VERTEX_INPUT_RATE_VERTEX, VK_FORMAT_R32G32B32_SFLOAT,
              sizeof(float)*3, sizeof(float)*6 },
        }
    );

    vkDestroyShaderModule(_vulkan->device, vmodule, nullptr);
    vkDestroyShaderModule(_vulkan->device, fmodule, nullptr);

    {
        VkDescriptorBufferInfo buffer_info = {};
        buffer_info.buffer = _state->matrix_buffer;
        buffer_info.offset = 0;
        buffer_info.range = VK_WHOLE_SIZE;

        VkWriteDescriptorSet descriptor_write = {};
        descriptor_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptor_write.pNext = nullptr;
        descriptor_write.dstSet = _state->descriptor_set;
        descriptor_write.dstBinding = 0;
        descriptor_write.dstArrayElement = 0;
        descriptor_write.descriptorCount = 1;
        descriptor_write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptor_write.pImageInfo = nullptr;
        descriptor_write.pBufferInfo = &buffer_info;
        descriptor_write.pTexelBufferView = nullptr;

        vkUpdateDescriptorSets(_vulkan->device, 1, &descriptor_write, 0, nullptr);
    }
}

void Update(void* _pstate,
            vktk::Context* _vulkan,
            iotk::input_t const* _input,
            iotk::input_t const* _lastInput)
{
}

void Draw(void* _pstate,
          vktk::Context* _vulkan,
          vktk::Swapchain const& _swapchain,
          VkSemaphore _acquire_semaphore)
{
    state_t* _state = (state_t*)_pstate;

    std::vector<std::size_t> signaled_fences = {};
    for (std::size_t index = 0; index < _state->render_semaphores.size(); ++index)
        if (vkGetFenceStatus(_vulkan->device, _state->render_semaphores[index].fence) == VK_SUCCESS)
            signaled_fences.push_back(index);
    for (std::size_t index : signaled_fences)
    {
        vkDestroyFence(_vulkan->device, _state->render_semaphores[index].fence, nullptr);
        vkDestroySemaphore(_vulkan->device, _state->render_semaphores[index].handle, nullptr);
        _state->render_semaphores.erase(_state->render_semaphores.begin() + index);
    }

    RenderSemaphore render_semaphore = {};
    {
        VkSemaphoreCreateInfo semaphore_info = {};
        semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        semaphore_info.pNext = nullptr;
        semaphore_info.flags = 0;
        CHECKCALL(vkCreateSemaphore,
                  _vulkan->device, &semaphore_info, nullptr, &render_semaphore.handle);

        VkFenceCreateInfo fence_info = {};
        fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fence_info.pNext = nullptr;
        fence_info.flags = 0;
        CHECKCALL(vkCreateFence,
                  _vulkan->device, &fence_info, nullptr, &render_semaphore.fence);
    }

    {
        vktk::CommandBuffer command_buffer = _vulkan->CreateCommandBuffer(_vulkan->present_queue);
        command_buffer.Begin();

        VkViewport viewport{};
        viewport.x = 0.f; viewport.y = 0.f;
        viewport.width = (float)_swapchain.size.width;
        viewport.height = (float)_swapchain.size.height;
        viewport.minDepth = 0.f; viewport.maxDepth = 1.f;
        vkCmdSetViewport(command_buffer, 0, 1, &viewport);

        VkRect2D scissor{ VkOffset2D{ 0, 0 }, _swapchain.size };
        vkCmdSetScissor(command_buffer, 0, 1, &scissor);

        VkClearValue clear_value{};
        clear_value.color.float32[0] = 0.f; clear_value.color.float32[1] = 1.f;
        clear_value.color.float32[2] = 1.f; clear_value.color.float32[3] = 0.f;
        clear_value.depthStencil.depth = 0.f;
        clear_value.depthStencil.stencil = 0;
        VkRenderPassBeginInfo rp_begin_info{};
        rp_begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp_begin_info.pNext = nullptr;
        rp_begin_info.renderPass = _swapchain.render_pass;
        rp_begin_info.framebuffer = _swapchain.framebuffers[_swapchain.index];
        rp_begin_info.renderArea = VkRect2D{
            VkOffset2D{ 0, 0 },
            _swapchain.size
        };
        rp_begin_info.clearValueCount = 1;
        rp_begin_info.pClearValues = &clear_value;

        vkCmdBeginRenderPass(command_buffer, &rp_begin_info, VK_SUBPASS_CONTENTS_INLINE);

        vkCmdBindIndexBuffer(command_buffer, _state->index_buffer, 0, VK_INDEX_TYPE_UINT32);
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(command_buffer, 0, 1, &_state->vertex_buffer.buffer, &offset);
        offset = sizeof(numtk::vec3f);
        vkCmdBindVertexBuffers(command_buffer, 1, 1, &_state->vertex_buffer.buffer, &offset);
        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _state->graphics_pipeline);

        vkCmdBindDescriptorSets(command_buffer,
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                _state->pipeline_layout, 0u,
                                1u, &_state->descriptor_set,
                                0u, nullptr);

        vkCmdDrawIndexed(command_buffer, 20*3, 1, 0, 0, 0);

        vkCmdEndRenderPass(command_buffer);

        command_buffer.End();

        VkSemaphore wait_semaphores[2] = {
            _acquire_semaphore,
            _state->upload_semaphore
        };

        VkPipelineStageFlags wait_dst_stages[2] = {
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
        };

        VkSubmitInfo submit_info{};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.pNext = nullptr;
        submit_info.waitSemaphoreCount =
            (_state->upload_semaphore == VK_NULL_HANDLE)
            ? 1u : 2u;
        submit_info.pWaitSemaphores = wait_semaphores;
        submit_info.pWaitDstStageMask = wait_dst_stages;
        submit_info.commandBufferCount = 1u;
        submit_info.pCommandBuffers = &command_buffer.handle;
        submit_info.signalSemaphoreCount = 1u;
        submit_info.pSignalSemaphores = &render_semaphore.handle;
        vkQueueSubmit(_vulkan->present_queue, 1, &submit_info, render_semaphore.fence);

        _state->dead_semaphores.push_back(_state->upload_semaphore);
        _state->upload_semaphore = VK_NULL_HANDLE;
    }

    VkPresentInfoKHR present_info{};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.pNext = nullptr;
    present_info.waitSemaphoreCount = 1u;
    present_info.pWaitSemaphores = &render_semaphore.handle;
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &_swapchain.handle;
    present_info.pImageIndices = &_swapchain.index;
    present_info.pResults = nullptr;
    CHECKCALL(_vulkan->fp.QueuePresentKHR, _vulkan->present_queue, &present_info);

    _state->render_semaphores.push_back(render_semaphore);
}

void Shutdown(void* _pstate, vktk::Context* _vulkan)
{
    state_t* _state = (state_t*)_pstate;

    vkDestroySemaphore(_vulkan->device, _state->copy_semaphore, nullptr);
    if (_state->upload_semaphore != VK_NULL_HANDLE)
        vkDestroySemaphore(_vulkan->device, _state->upload_semaphore, nullptr);

    for (VkSemaphore semaphore : _state->dead_semaphores)
        vkDestroySemaphore(_vulkan->device, semaphore, nullptr);

    for (RenderSemaphore render_semaphore : _state->render_semaphores)
    {
        CHECKCALL(vkWaitForFences, _vulkan->device, 1, &render_semaphore.fence, VK_TRUE, ~0ull);
        vkDestroySemaphore(_vulkan->device, render_semaphore.handle, nullptr);
        vkDestroyFence(_vulkan->device, render_semaphore.fence, nullptr);
    }

    vkUnmapMemory(_vulkan->device, _state->staging_buffer.memory);
    _vulkan->DestroyBuffer(_state->index_buffer);
    _vulkan->DestroyBuffer(_state->vertex_buffer);
    _vulkan->DestroyBuffer(_state->matrix_buffer);
    _vulkan->DestroyBuffer(_state->staging_buffer);
    vkDestroyPipeline(_vulkan->device, _state->graphics_pipeline, nullptr);
    vkDestroyPipelineLayout(_vulkan->device, _state->pipeline_layout, nullptr);
    vkDestroyDescriptorSetLayout(_vulkan->device, _state->descset_layout, nullptr);
    vkDestroyDescriptorPool(_vulkan->device, _state->descriptor_pool, nullptr);
}


}

namespace Icosphere
{
Interface::Interface() :
    ConstantResources{ ::ConstantResources },
    RenderResources{ ::RenderResources },
    Update{ ::Update },
    Draw{ ::Draw },
    Shutdown{ ::Shutdown },
    state{ new state_t{} }
{}

Interface::~Interface()
{
    delete (state_t*)state;
}

}
