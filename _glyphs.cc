#include "_glyphs.hh"

#include "vktk.hh"
#include "loader/iotk.hpp"

#include <iostream>

#define BMPTK_IMPLEMENTATION
#include "bmp/bmptk.h"
#include <vector>
#include <fstream>

static std::vector<uint8_t> LoadFile(char const* _path)
{
    std::ifstream source_file(_path, std::ios_base::binary);
    if (!source_file.is_open())
    {
        std::cout << "Error loading file " << _path << std::endl;
        return {};
    }

    source_file.seekg(0, std::ios_base::end);
    size_t size = source_file.tellg();
    source_file.seekg(0, std::ios_base::beg);

    std::vector<uint8_t> memory{};
    memory.resize(size);

    source_file.read((char*)memory.data(), size);

    return memory;
}

namespace {

struct state_t
{
    VkPipeline graphics_pipeline;
    VkPipelineLayout pipeline_layout;
    vktk::Texture font;
    VkImageView font_view;
    vktk::Buffer staging;
    VkSampler sampler;
    VkDescriptorPool descriptor_pool;
    VkDescriptorSet descriptor_set;
    VkDescriptorSetLayout descset_layout;

    uint32_t last_key;

    std::vector<struct MovingGlyph*> glyphs;
    std::string display_string;
};

struct Glyph
{
    float x, y;
    uint32_t char_index;
};

struct MovingGlyph
{
    static void _LogicUpdate(void* _state, state_t* _context, iotk::input_t const* _input) {
        ((MovingGlyph*)_state)->LogicUpdate(_context, _input);
    }

    float x, y;
    uint32_t char_index;
    MovingGlyph(uint32_t _char_index) :
        x{ (float)_char_index / 128.f }, y{ 0.f }, char_index{ _char_index }
    {}
    void LogicUpdate(state_t*, iotk::input_t const* _input) {
        y += _input->time_delta * 0.1f;
    }
};

void ConstantResources(void* _pstate,
                       vktk::Context* _vulkan)
{
    state_t* _state = (state_t*)_pstate;

    VkDescriptorPoolSize descpool_sizes[1] = {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 },
    };

    VkDescriptorPoolCreateInfo descpool_create{};
    descpool_create.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descpool_create.pNext = nullptr;
    descpool_create.flags = 0u;
    descpool_create.maxSets = 1u;
    descpool_create.poolSizeCount = sizeof(descpool_sizes) / sizeof(descpool_sizes[0]);
    descpool_create.pPoolSizes = descpool_sizes;
    CHECKCALL(vkCreateDescriptorPool, _vulkan->device,
              &descpool_create, nullptr, &_state->descriptor_pool);

    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    binding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo descset_create{};
    descset_create.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descset_create.pNext = nullptr;
    descset_create.flags = 0u;
    descset_create.bindingCount = 1;
    descset_create.pBindings = &binding;
    CHECKCALL(vkCreateDescriptorSetLayout, _vulkan->device,
              &descset_create, nullptr, &_state->descset_layout);

    VkDescriptorSetAllocateInfo descset_allocate{};
    descset_allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    descset_allocate.pNext= nullptr;
    descset_allocate.descriptorPool = _state->descriptor_pool;
    descset_allocate.descriptorSetCount = 1u;
    descset_allocate.pSetLayouts = &_state->descset_layout;
    CHECKCALL(vkAllocateDescriptorSets, _vulkan->device,
              &descset_allocate, &_state->descriptor_set);

    VkSamplerCreateInfo sampler_create{};
    sampler_create.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_create.pNext = nullptr;
    sampler_create.flags = 0u;
    sampler_create.magFilter = VK_FILTER_NEAREST;
    sampler_create.minFilter = VK_FILTER_NEAREST;
    sampler_create.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler_create.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_create.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_create.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    CHECKCALL(vkCreateSampler, _vulkan->device,
              &sampler_create, nullptr, &_state->sampler);

    VkPushConstantRange push_constants{};
    push_constants.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    push_constants.offset = 0;
    push_constants.size = sizeof(MovingGlyph);

    VkPipelineLayoutCreateInfo pipeline_create{};
    pipeline_create.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_create.pNext = nullptr;
    pipeline_create.flags = 0u;
    pipeline_create.setLayoutCount = 1u;
    pipeline_create.pSetLayouts = &_state->descset_layout;
    pipeline_create.pushConstantRangeCount = 1u;
    pipeline_create.pPushConstantRanges = &push_constants;

    CHECKCALL(vkCreatePipelineLayout, _vulkan->device, &pipeline_create, nullptr,
              &_state->pipeline_layout);

    std::vector<uint8_t> fontfile = LoadFile("../font.bmp");
    bmptk::BitmapFile bmp{};
    bmptk::Result result = bmptk::LoadBMP(fontfile.data(), &bmp);

    _state->staging = _vulkan->CreateBuffer(
        abs(bmp.width)*abs(bmp.height),
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT);

    void* mappedData = nullptr;
    CHECKCALL(vkMapMemory, _vulkan->device,
              _state->staging.alloc.memory, 0u, VK_WHOLE_SIZE, 0u, &mappedData);

    uint8_t* imageBuffer = (uint8_t*)mappedData;
    for (uint32_t y = 0; y < abs(bmp.height); ++y)
    {
        for (uint32_t x = 0; x < abs(bmp.width); ++x)
        {
            bmptk::PixelValue src = bmptk::GetPixel(&bmp, x, y);
            uint8_t* dst = imageBuffer + x + y*abs(bmp.width);
            *dst = src.red;
        }
    }

    vkUnmapMemory(_vulkan->device, _state->staging.alloc.memory);

    _state->font = _vulkan->CreateTexture(
        abs(bmp.width), abs(bmp.height), 1,
        VK_IMAGE_TYPE_2D, VK_FORMAT_R8_UNORM,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT
        | VK_IMAGE_USAGE_SAMPLED_BIT);

    VkImageViewCreateInfo image_view_create{};
    image_view_create.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    image_view_create.pNext = nullptr;
    image_view_create.flags = 0u;
    image_view_create.image = _state->font.image;
    image_view_create.viewType = VK_IMAGE_VIEW_TYPE_2D;
    image_view_create.format = VK_FORMAT_R8_UNORM;
    image_view_create.components = VkComponentMapping{};
    image_view_create.subresourceRange = VkImageSubresourceRange{
        VK_IMAGE_ASPECT_COLOR_BIT,
        0u, 1u, 0u, 1u
    };
    CHECKCALL(vkCreateImageView, _vulkan->device,
              &image_view_create, nullptr, &_state->font_view);

    vktk::CommandBuffer command_buffer = _vulkan->CreateCommandBuffer(_vulkan->present_queue);
    command_buffer.Begin();

    VkImageMemoryBarrier image_memory_barrier{};
    image_memory_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    image_memory_barrier.pNext = nullptr;
    image_memory_barrier.srcAccessMask = VK_ACCESS_NONE;
    image_memory_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    image_memory_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_memory_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    image_memory_barrier.srcQueueFamilyIndex = 0u;
    image_memory_barrier.dstQueueFamilyIndex = 0u;
    image_memory_barrier.image = _state->font.image;
    image_memory_barrier.subresourceRange = VkImageSubresourceRange{
        VK_IMAGE_ASPECT_COLOR_BIT,
        0u, 1u, 0u, 1u
    };

    VkBufferMemoryBarrier buffer_memory_barrier{};
    buffer_memory_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    buffer_memory_barrier.pNext = nullptr;
    buffer_memory_barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
    buffer_memory_barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    buffer_memory_barrier.srcQueueFamilyIndex = 0u;
    buffer_memory_barrier.dstQueueFamilyIndex = 0u;
    buffer_memory_barrier.buffer = _state->staging.buffer;
    buffer_memory_barrier.offset = 0u;
    buffer_memory_barrier.size = abs(bmp.width)*abs(bmp.height);

    vkCmdPipelineBarrier(command_buffer,
                         VK_PIPELINE_STAGE_HOST_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0u,
                         0u, nullptr,
                         1u, &buffer_memory_barrier,
                         1u, &image_memory_barrier);

    VkBufferImageCopy copy_region{};
    copy_region.bufferOffset = 0u;
    copy_region.bufferRowLength = 0u;
    copy_region.bufferImageHeight = 0u;
    copy_region.imageSubresource = VkImageSubresourceLayers {
        VK_IMAGE_ASPECT_COLOR_BIT,
        0u, 0u, 1u
    };
    copy_region.imageOffset = VkOffset3D{ 0u, 0u, 0u };
    copy_region.imageExtent = VkExtent3D{
        (uint32_t)abs(bmp.width),
        (uint32_t)abs(bmp.height), 1u };

    vkCmdCopyBufferToImage(command_buffer,
                           _state->staging.buffer,
                           _state->font.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1,
                           &copy_region);

    image_memory_barrier.oldLayout = image_memory_barrier.newLayout;
    image_memory_barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    image_memory_barrier.srcAccessMask = image_memory_barrier.dstAccessMask;
    image_memory_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(command_buffer,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0u,
                         0u, nullptr,
                         0u, nullptr,
                         1u, &image_memory_barrier);

    command_buffer.End();

    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.pNext = nullptr;
    submit_info.waitSemaphoreCount = 0u;
    submit_info.pWaitSemaphores = nullptr;
    submit_info.pWaitDstStageMask = nullptr;
    submit_info.commandBufferCount = 1u;
    submit_info.pCommandBuffers = &command_buffer.handle;
    submit_info.signalSemaphoreCount = 0u;
    submit_info.pSignalSemaphores = nullptr;
    vkQueueSubmit(_vulkan->present_queue, 1, &submit_info, VK_NULL_HANDLE);
}

void RenderResources(void* _pstate,
                     vktk::Context* _vulkan,
                     vktk::Swapchain const& _swapchain)
{
    state_t* _state = (state_t*)_pstate;

    if (_state->graphics_pipeline != VK_NULL_HANDLE)
        vkDestroyPipeline(_vulkan->device, _state->graphics_pipeline, nullptr);

    constexpr uint32_t kStageCount = 2u;

    static char const vshader[] = R"_(
    #version 450
    const vec2 kTriVertices[] = vec2[4]( vec2(-1.0, -1.0),
                                         vec2(-1.0, 1.0),
                                         vec2(1.0, -1.0),
                                         vec2(1.0, 1.0) );

    layout(location = 0) out vec2 texCoords;
    layout(push_constant) uniform DataBlock {
        float x;
        float y;
        uint char_index;
    };

    void main() {
        vec2 quad = kTriVertices[gl_VertexIndex];
        vec2 glyphloc = vec2(x, y) * 2.0 - vec2(1.0);
        gl_Position = vec4(glyphloc + ((quad + 1.0) / 16.0), 0.0, 1.0);

        uint char_row = char_index >> 4;
        uint char_col = char_index & 0xf;
        texCoords = ((quad * 0.5 + vec2(0.5)) + vec2(float(char_row), float(char_col))) / 16.0;
    }
    )_";
    static uint32_t const vsize = sizeof(vshader) - 1u;

    static char const fshader[] = R"(
    #version 450
    layout(location = 0) in vec2 texCoords;
    layout(location = 0) out vec4 color;
    layout(set = 0, binding = 0) uniform sampler2D font;
    void main() {
        float fontv = texture(font, texCoords).x;
        color = vec4(0.5, 0.5, 1.0, 1.0) * fontv;
    }
    )";
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
            { fmodule, VK_SHADER_STAGE_FRAGMENT_BIT }
        } );

    VkDescriptorPoolSize descpool_sizes[1] = {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 },
    };

    vkDestroyShaderModule(_vulkan->device, vmodule, nullptr);
    vkDestroyShaderModule(_vulkan->device, fmodule, nullptr);
}

bool Update(void* _pstate,
            vktk::Context*,
            iotk::input_t const* _input,
            iotk::input_t const* _lastInput)
{
    state_t* _state = (state_t*)_pstate;

    for (MovingGlyph* glyph : _state->glyphs)
        glyph->LogicUpdate(_state, _input);

    if (_input->key_down[iotk::eKey::kEscape])
    {
        std::cout << "escape down" << std::endl;
        return false;
    }

    for (uint32_t index = 1u; index < 256; ++index)
    {
        if (_lastInput->key_down[index] != _input->key_down[index])
        {
            if (_input->key_down[index])
            {
                std::cout << "key down " << std::hex << index << std::endl;
                if (index > (uint32_t)iotk::kASCIIBegin)
                {
                    uint32_t char_index = index - (uint32_t)iotk::kASCIIBegin;
                    _state->last_key = char_index;

                    _state->display_string.push_back((char)index);

                    MovingGlyph* glyph = new MovingGlyph{ char_index };
                    _state->glyphs.push_back(glyph);
                }
            }
            else
                std::cout << "key up " << std::hex << index << std::endl;
        }
    }

    return true;
}

void Draw(void* _pstate,
          vktk::Context* _vulkan,
          vktk::Swapchain const& _swapchain,
          VkSemaphore _acquire_semaphore)
{
    state_t* _state = (state_t*)_pstate;

    VkDescriptorImageInfo image_info{};
    image_info.sampler = _state->sampler;
    image_info.imageView = _state->font_view;
    image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet desc_write{};
    desc_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    desc_write.pNext = nullptr;
    desc_write.dstSet = _state->descriptor_set;
    desc_write.dstBinding = 0u;
    desc_write.dstArrayElement = 0u;
    desc_write.descriptorCount = 1u;
    desc_write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    desc_write.pImageInfo = &image_info;
    desc_write.pBufferInfo = nullptr;
    desc_write.pTexelBufferView = nullptr;
    vkUpdateDescriptorSets(_vulkan->device, 1u, &desc_write, 0u, nullptr);

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

    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _state->graphics_pipeline);
    vkCmdBindDescriptorSets(command_buffer,
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            _state->pipeline_layout, 0u,
                            1u, &_state->descriptor_set,
                            0u, nullptr);

    for (MovingGlyph const* glyph : _state->glyphs)
    {
        vkCmdPushConstants(command_buffer,
                           _state->pipeline_layout,
                           VK_SHADER_STAGE_VERTEX_BIT,
                           0u, sizeof(MovingGlyph), glyph);

        vkCmdDraw(command_buffer, 4u, 1u, 0u, 0u);
    }

    for (uint32_t index = 0u; index < _state->display_string.size(); ++index)
    {
        float x = (float)(index & 0xf) / 16.f;
        float y = (float)(index >> 4u) / 16.f;
        Glyph glyph{ x, y, (uint32_t)_state->display_string[index] };

        vkCmdPushConstants(command_buffer,
                           _state->pipeline_layout,
                           VK_SHADER_STAGE_VERTEX_BIT,
                           0u, sizeof(MovingGlyph), &glyph);

        vkCmdDraw(command_buffer, 4u, 1u, 0u, 0u);
    }

    vkCmdEndRenderPass(command_buffer);

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

void Shutdown(void* _pstate,
              vktk::Context* _vulkan)
{
    state_t* _state = (state_t*)_pstate;

    for (MovingGlyph* glyph : _state->glyphs)
        delete glyph;

    vkDestroyPipeline(_vulkan->device, _state->graphics_pipeline, nullptr);
    _state->graphics_pipeline = VK_NULL_HANDLE;
    vkDestroyPipelineLayout(_vulkan->device, _state->pipeline_layout, nullptr);

    vkDestroyImage(_vulkan->device, _state->font.image, nullptr);
    vkFreeMemory(_vulkan->device, _state->font.memory, nullptr);
    vkDestroyImageView(_vulkan->device, _state->font_view, nullptr);
    vkDestroyBuffer(_vulkan->device, _state->staging.buffer, nullptr);
    //vkFreeMemory(_vulkan->device, _state->staging.alloc.memory, nullptr);

    vkDestroySampler(_vulkan->device, _state->sampler, nullptr);
    vkFreeDescriptorSets(_vulkan->device, _state->descriptor_pool, 1, &_state->descriptor_set);
    vkDestroyDescriptorPool(_vulkan->device, _state->descriptor_pool, nullptr);
    vkDestroyDescriptorSetLayout(_vulkan->device, _state->descset_layout, nullptr);
}

}

namespace Glyphs
{

Interface::Interface() :
    ConstantResources{ ::ConstantResources },
    RenderResources{ ::RenderResources },
    Update{ ::Update },
    Draw{ ::Draw },
    Shutdown{ ::Shutdown },
    state{ new state_t{} }
{
    ((state_t*)state)->graphics_pipeline = VK_NULL_HANDLE;
}

Interface::~Interface()
{
    delete (state_t*)state;
}

}
