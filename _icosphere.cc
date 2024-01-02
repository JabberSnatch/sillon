#include "_icosphere.hh"

#include "loader/iotk.hpp"
#include "numtk.hh"
#include "vktk.hh"

#include <iostream>

namespace {

struct state_t
{
    VkDescriptorSetLayout descset_layout;
    VkPipelineLayout pipeline_layout;
    VkPipeline graphics_pipeline;
    vktk::Buffer index_buffer;
    vktk::Buffer vertex_buffer;
    vktk::Buffer matrix_buffer;
    vktk::Buffer staging_buffer;

    VkDescriptorPool descriptor_pool;
    VkDescriptorSet descriptor_set;
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

    _state->vertex_buffer = _vulkan->CreateBuffer(
        12 * sizeof(float) * 3,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT
        | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

    _state->matrix_buffer = _vulkan->CreateBuffer(
        16 * sizeof(float) * 100,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT
        | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);

    void* staging_area = nullptr;
    CHECKCALL(vkMapMemory,
              _vulkan->device, _state->staging_buffer.memory,
              0, VK_WHOLE_SIZE, 0, &staging_area);

    numtk::vec3f* vertices = (numtk::vec3f*)staging_area;
    numtk::vec3u* triangles = (numtk::vec3u*)(vertices+12);
    numtk::mat4f* matrices = (numtk::mat4f*)(triangles+20);

    {
        matrices[0] =
            numtk::mat4f::View({ 0.f, 0.f, 1.f }, { 0.f, 1.f, 0.f }, { 0.f, 0.f, -3.f })
            * numtk::mat4f::Perspective(0.1f, 100.f, 90.f, 1.f);
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
        vertices[0] = spherical_to_cartesian({ kPoleLat, 0.f, 1.f });
        vertices[1] = spherical_to_cartesian({ kCommonLat, kLongIncrement*0.f, 1.f });
        vertices[2] = spherical_to_cartesian({ -kCommonLat, kLongIncrement*1.f, 1.f });
        vertices[3] = spherical_to_cartesian({ kCommonLat, kLongIncrement*2.f, 1.f });
        vertices[4] = spherical_to_cartesian({ -kCommonLat, kLongIncrement*3.f, 1.f });
        vertices[5] = spherical_to_cartesian({ kCommonLat, kLongIncrement*4.f, 1.f });
        vertices[6] = spherical_to_cartesian({ -kCommonLat, kLongIncrement*5.f, 1.f });
        vertices[7] = spherical_to_cartesian({ kCommonLat, kLongIncrement*6.f, 1.f });
        vertices[8] = spherical_to_cartesian({ -kCommonLat, kLongIncrement*7.f, 1.f });
        vertices[9] = spherical_to_cartesian({ kCommonLat, kLongIncrement*8.f, 1.f });
        vertices[10] = spherical_to_cartesian({ -kCommonLat, kLongIncrement*9.f, 1.f });
        vertices[11] = spherical_to_cartesian({ -kPoleLat, 0.f, 1.f });
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
    VkSemaphore queue_transfer_semaphore = {};
    {
        VkSemaphoreCreateInfo create_info = {};
        create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        create_info.pNext = nullptr;
        create_info.flags = 0;
        CHECKCALL(vkCreateSemaphore,
                  _vulkan->device, &create_info, nullptr, &queue_transfer_semaphore);
    }

    copy_commands.Begin();
    present_commands.Begin();

    {
        VkBufferMemoryBarrier base_barrier = {};
        base_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        base_barrier.pNext = nullptr;
        base_barrier.srcAccessMask = VK_ACCESS_NONE;
        base_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        base_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        base_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        base_barrier.buffer = VK_NULL_HANDLE;
        base_barrier.offset = 0;
        base_barrier.size = VK_WHOLE_SIZE;

        VkBufferMemoryBarrier buffer_barriers[3];
        buffer_barriers[0] = base_barrier;
        buffer_barriers[0].buffer = _state->vertex_buffer;
        buffer_barriers[1] = base_barrier;
        buffer_barriers[1].buffer = _state->index_buffer;
        buffer_barriers[2] = base_barrier;
        buffer_barriers[2].buffer = _state->matrix_buffer;

        vkCmdPipelineBarrier(
            copy_commands,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,
            0, nullptr,
            3, buffer_barriers,
            0, nullptr);
    }

    uint64_t offset = 0;
    copy_commands.BufferCopy(_state->vertex_buffer, 0,
                             _state->staging_buffer, offset,
                             sizeof(numtk::vec3f)*12);
    offset += sizeof(numtk::vec3f)*12;
    copy_commands.BufferCopy(_state->index_buffer, 0,
                             _state->staging_buffer, offset,
                             sizeof(numtk::vec3u)*20);
    offset += sizeof(numtk::vec3u)*20;
    copy_commands.BufferCopy(_state->matrix_buffer, 0,
                             _state->staging_buffer, offset,
                             sizeof(numtk::mat4f));

    {
        VkBufferMemoryBarrier base_barrier = {};
        base_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        base_barrier.pNext = nullptr;
        base_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        base_barrier.dstAccessMask = VK_ACCESS_NONE;
        base_barrier.srcQueueFamilyIndex = _vulkan->copy_queue.family_index;
        base_barrier.dstQueueFamilyIndex = _vulkan->present_queue.family_index;
        base_barrier.buffer = VK_NULL_HANDLE;
        base_barrier.offset = 0;
        base_barrier.size = VK_WHOLE_SIZE;

        VkBufferMemoryBarrier buffer_barriers[3];
        buffer_barriers[0] = base_barrier;
        buffer_barriers[0].buffer = _state->vertex_buffer;
        buffer_barriers[0].dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
        buffer_barriers[1] = base_barrier;
        buffer_barriers[1].buffer = _state->index_buffer;
        buffer_barriers[1].dstAccessMask = VK_ACCESS_INDEX_READ_BIT;
        buffer_barriers[2] = base_barrier;
        buffer_barriers[2].buffer = _state->matrix_buffer;
        buffer_barriers[2].dstAccessMask = VK_ACCESS_UNIFORM_READ_BIT;

        vkCmdPipelineBarrier(
            copy_commands,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            0,
            0, nullptr,
            3, buffer_barriers,
            0, nullptr);

        vkCmdPipelineBarrier(
            present_commands,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
            0,
            0, nullptr,
            3, buffer_barriers,
            0, nullptr);
    }

    copy_commands.End();
    present_commands.End();

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
        submit_info.pSignalSemaphores = &queue_transfer_semaphore;
        vkQueueSubmit(_vulkan->copy_queue, 1, &submit_info, VK_NULL_HANDLE);
    }

    {
        VkSubmitInfo submit_info{};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.pNext = nullptr;
        submit_info.waitSemaphoreCount = 1u;
        submit_info.pWaitSemaphores = &queue_transfer_semaphore;
        submit_info.pWaitDstStageMask = nullptr;
        submit_info.commandBufferCount = 1u;
        submit_info.pCommandBuffers = &present_commands.handle;
        submit_info.signalSemaphoreCount = 0u;
        submit_info.pSignalSemaphores = nullptr;
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
    layout(std140, set = 0, binding = 0) uniform MatrixBlock {
        mat4 mvp;
    };
    void main() {
        vec4 position = mvp * vec4(iVS_position, 1.0);
        gl_Position = position / position.w;
    }
    )_";
    static uint32_t const vsize = sizeof(vshader) - 1u;

    static char const fshader[] = R"_(
    #version 450
    layout(location = 0) out vec4 oFS_color;
    void main() {
        oFS_color = vec4(1.0, 0.0, 1.0, 1.0);
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

    _state->graphics_pipeline = _vulkan->CreatePipeline(
        _state->pipeline_layout,
        _swapchain.render_pass,
        {
            { vmodule, VK_SHADER_STAGE_VERTEX_BIT },
            { fmodule, VK_SHADER_STAGE_FRAGMENT_BIT },
        },
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        {
            { 0, 0, VK_VERTEX_INPUT_RATE_VERTEX, VK_FORMAT_R32G32B32_SFLOAT, 0, sizeof(float)*3 }
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

    {
        vktk::CommandBuffer command_buffer = _vulkan->CreateCommandBuffer(_vulkan->present_queue);
        command_buffer.Begin();

        VkImageMemoryBarrier image_barrier = {};
        image_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        image_barrier.pNext = nullptr;
        image_barrier.srcAccessMask = 0;
        image_barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        image_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        image_barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        image_barrier.srcQueueFamilyIndex = 0;
        image_barrier.dstQueueFamilyIndex = 0;
        image_barrier.image = _swapchain.images[_swapchain.index];
        image_barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

        vkCmdPipelineBarrier(
            command_buffer,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &image_barrier);

        command_buffer.End();

        VkPipelineStageFlags wait_dst_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submit_info{};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.pNext = nullptr;
        submit_info.waitSemaphoreCount = 1u;
        submit_info.pWaitSemaphores = &_acquire_semaphore;
        submit_info.pWaitDstStageMask = &wait_dst_stage;
        submit_info.commandBufferCount = 1u;
        submit_info.pCommandBuffers = &command_buffer.handle;
        submit_info.signalSemaphoreCount = 0u;
        submit_info.pSignalSemaphores = nullptr;
        vkQueueSubmit(_vulkan->present_queue, 1, &submit_info, VK_NULL_HANDLE);
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
        static VkDeviceSize const offset = 0;
        vkCmdBindVertexBuffers(command_buffer, 0, 1, &_state->vertex_buffer.buffer, &offset);
        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _state->graphics_pipeline);

        vkCmdBindDescriptorSets(command_buffer,
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                _state->pipeline_layout, 0u,
                                1u, &_state->descriptor_set,
                                0u, nullptr);

        vkCmdDrawIndexed(command_buffer, 20*3, 1, 0, 0, 0);

        vkCmdEndRenderPass(command_buffer);

        command_buffer.End();

        VkPipelineStageFlags wait_dst_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submit_info{};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.pNext = nullptr;
        submit_info.waitSemaphoreCount = 0u;
        submit_info.pWaitSemaphores = nullptr;//&_acquire_semaphore;
        submit_info.pWaitDstStageMask = nullptr;//&wait_dst_stage;
        submit_info.commandBufferCount = 1u;
        submit_info.pCommandBuffers = &command_buffer.handle;
        submit_info.signalSemaphoreCount = 0u;
        submit_info.pSignalSemaphores = nullptr;
        vkQueueSubmit(_vulkan->present_queue, 1, &submit_info, VK_NULL_HANDLE);
    }

    {
        vktk::CommandBuffer command_buffer = _vulkan->CreateCommandBuffer(_vulkan->present_queue);
        command_buffer.Begin();

        VkImageMemoryBarrier image_barrier = {};
        image_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        image_barrier.pNext = nullptr;
        image_barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        image_barrier.dstAccessMask = 0;
        image_barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        image_barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        image_barrier.srcQueueFamilyIndex = 0;
        image_barrier.dstQueueFamilyIndex = 0;
        image_barrier.image = _swapchain.images[_swapchain.index];
        image_barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

        vkCmdPipelineBarrier(
            command_buffer,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &image_barrier);

        command_buffer.End();

        VkPipelineStageFlags wait_dst_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submit_info{};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.pNext = nullptr;
        submit_info.waitSemaphoreCount = 0u;
        submit_info.pWaitSemaphores = nullptr;//&_acquire_semaphore;
        submit_info.pWaitDstStageMask = nullptr;//&wait_dst_stage;
        submit_info.commandBufferCount = 1u;
        submit_info.pCommandBuffers = &command_buffer.handle;
        submit_info.signalSemaphoreCount = 0u;
        submit_info.pSignalSemaphores = nullptr;
        vkQueueSubmit(_vulkan->present_queue, 1, &submit_info, VK_NULL_HANDLE);
    }

    VkPresentInfoKHR present_info{};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.pNext = nullptr;
    present_info.waitSemaphoreCount = 0u;
    present_info.pWaitSemaphores = nullptr;
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &_swapchain.handle;
    present_info.pImageIndices = &_swapchain.index;
    present_info.pResults = nullptr;
    CHECKCALL(_vulkan->fp.QueuePresentKHR, _vulkan->present_queue, &present_info);
}

void Shutdown(void* _pstate, vktk::Context* _vulkan)
{
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
