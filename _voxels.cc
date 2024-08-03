#include "_voxels.hh"

#define ZIPTK_IMPLEMENTATION
#define MCTK_IMPLEMENTATION
#include "mctk/mctk.hh"

#include "systk.hh"
#include "numtk.hh"
#include "vktk.hh"

namespace {

struct state_t
{
    vktk::Texture chunk;
    VkImageView chunk_view;
    VkPipeline graphics_pipeline;
    VkPipelineLayout pipeline_layout;
    VkSampler sampler;
    VkDescriptorPool descriptor_pool;
    VkDescriptorSet descriptor_set;
    VkDescriptorSetLayout descset_layout;
};

void ConstantResources(void* _pstate, vktk::Context* _vulkan)
{
    state_t* _state = (state_t*)_pstate;

    std::vector<uint8_t> testfile = systk::LoadFile("../r.-1.6.mca");
    mctk::MCAFileData mca_data = mctk::MCADecode(testfile.data(), testfile.size());

    std::vector<mctk::NBTField> chunk_fields = mctk::MCAPackedNBTDecode(mca_data.chunks[700]);
    if (chunk_fields.empty())
        return;

    using namespace mctk;

    // -6
    // 213

    // -1
    // 6

    // -32 -> -1
    // 192 -> 223

    // 26
    // 21
    // -> 698

    NBTCompound const& root = *(NBTCompound const*)chunk_fields[0].payload;
    for (NBTField const& field0 : root)
    {
        if (field0.name == "Status"
            && field0.type == TAG_String
            && *(NBTString const*)field0.payload != "full")
            std::cout << "incomplete chunk, Status is "
                      << *(NBTString const*)field0.payload << std::endl;

        if (field0.name == "sections"
            && field0.type == TAG_List)
        {
            NBTList const& sections = *(NBTList const*)field0.payload;
            NBTCompound const* compounds = (NBTCompound const*)sections.data;
            for (uint32_t section_index = 0u; section_index < sections.size; ++section_index)
            {
                NBTCompound const& compound = compounds[section_index];
                for (NBTField const& field1 : compound)
                {
                    if (field1.name == "Y"
                        && field1.type == TAG_Byte)
                    {
                        std::cout << "Y " << (int32_t)*(int8_t const*)field1.payload << std::endl;
                        if (*(int8_t const*)field1.payload != 1)
                            break;
                    }

                    if (field1.name == "block_states"
                        && field1.type == TAG_Compound)
                    {
                        NBTCompound const& block_states = *(NBTCompound const*)field1.payload;
                        NBTLongArray const* block_data_field = nullptr;
                        uint32_t palette_size = 0u;
                        std::vector<std::string> palette_data = {};

                        for (NBTField const& field2 : block_states)
                        {
                            if (field2.name == "palette"
                                && field2.type == TAG_List)
                            {
                                NBTList const& palette = *((NBTList const*)field2.payload);
                                NBTCompound const* blocks = (NBTCompound const*)palette.data;
                                palette_size = palette.size;

                                for (uint32_t block_index = 0u;
                                     block_index < palette_size;
                                     ++block_index)
                                {
                                    NBTCompound const& block = blocks[block_index];

                                    for (NBTField const& field3 : block)
                                    {
                                        if (field3.name == "Name"
                                            && field3.type == TAG_String)
                                        {
#if 0
                                            std::cout << "\t"
                                                      << *(NBTString const*)field3.payload
                                                      << std::endl;
#endif
                                            palette_data.push_back(
                                                (*(NBTString const*)field3.payload).substr(10));
                                        }
                                    }
                                }
                            }

                            if (field2.name == "data"
                                && field2.type == TAG_Long_Array)
                                block_data_field = (NBTLongArray const*)field2.payload;
                        }

                        if (palette_size == 1)
                        {
                            std::cout << "solid chunk" << std::endl;
                            continue;
                        }

                        NBTLongArray const& block_data = *block_data_field;
                        uint32_t bit_count = numtk::log2(palette_size);
                        if (palette_size - (1 << bit_count))
                            ++bit_count;
                        bit_count = std::max(bit_count, 4u);
#if 0
                        std::cout << bit_count << std::endl;
                        uint32_t long_count = 4096 / (64/bit_count);
                        std::cout << long_count << " " << block_data.size() << std::endl;
                        for (uint32_t index = 0u; index < block_data.size(); ++index)
                            std::cout << std::hex << block_data[index] << " ";
                        std::cout << std::dec << std::endl;
#endif

                        _state->chunk = _vulkan->CreateTexture(
                            16, 16, 16,
                            VK_IMAGE_TYPE_3D, VK_FORMAT_R8_UNORM,
                            VK_IMAGE_USAGE_TRANSFER_DST_BIT
                            | VK_IMAGE_USAGE_SAMPLED_BIT,
                            [&block_data, &palette_data, bit_count](void* mapped_data) {
                                uint64_t block_index = 0;
                                uint32_t data_index = 0;
                                uint8_t data_offset = 0;
                                uint8_t* blocks = (uint8_t*)mapped_data;
                                while (block_index < 4096 && data_index < block_data.size())
                                {
                                    uint16_t block_id = (uint16_t)
                                        ((block_data[data_index] >> data_offset)
                                         & ((1ull << bit_count) - 1));

                                    uint32_t x = block_index % 16;
                                    uint32_t z = (block_index / 16) % 16;
                                    uint32_t y = (block_index / 256) % 16;

                                    blocks[y * 256 + z * 16 + x] =
#if 1
                                        (uint8_t)
                                        (((float)block_id / (float)palette_data.size()) * 255.f);
#else
                                    ((palette_data[block_id] != "air")
                                     && (palette_data[block_id] != "glow_lichen"))
                                        ? 255
                                        : 0;
#endif
                                    data_offset += bit_count;
                                    if (data_offset > 64)
                                    {
                                        data_offset = 0;
                                        ++data_index;
                                    }
                                    ++block_index;
                                }

                                #if 1
                                int counter = 0u;
                                block_index = 0;
                                data_index = 0;
                                data_offset = 0;
                                while (block_index < 4096 && data_index < block_data.size())
                                {
                                    uint16_t block_id = (uint16_t)
                                        ((block_data[data_index] >> data_offset)
                                         & ((1ull << bit_count) - 1));

                                    uint32_t x = block_index % 16;
                                    uint32_t z = (block_index / 16) % 16;
                                    uint32_t y = (block_index / 256) % 16;

                                    std::cout << palette_data[block_id] << " ";
                                    if(++counter > 16)
                                        std::cout << std::endl;
                                    counter = counter % 16;

                                    data_offset += bit_count;
                                    if (data_offset > 64)
                                    {
                                        data_offset = 0;
                                        ++data_index;
                                    }
                                    ++block_index;
                                }
                                std::cout << std::endl;
                                #endif

                            });

                    }
                }
            }
        }
    }

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

    VkPipelineLayoutCreateInfo pipeline_create{};
    pipeline_create.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_create.pNext = nullptr;
    pipeline_create.flags = 0u;
    pipeline_create.setLayoutCount = 1u;
    pipeline_create.pSetLayouts = &_state->descset_layout;
    pipeline_create.pushConstantRangeCount = 0;
    pipeline_create.pPushConstantRanges = nullptr;
    CHECKCALL(vkCreatePipelineLayout, _vulkan->device, &pipeline_create, nullptr,
              &_state->pipeline_layout);

    VkImageViewCreateInfo image_view_create{};
    image_view_create.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    image_view_create.pNext = nullptr;
    image_view_create.flags = 0u;
    image_view_create.image = _state->chunk.image;
    image_view_create.viewType = VK_IMAGE_VIEW_TYPE_3D;
    image_view_create.format = VK_FORMAT_R8_UNORM;
    image_view_create.components = VkComponentMapping{};
    image_view_create.subresourceRange = VkImageSubresourceRange{
        VK_IMAGE_ASPECT_COLOR_BIT,
        0u, 1u, 0u, 1u
    };
    CHECKCALL(vkCreateImageView, _vulkan->device,
              &image_view_create, nullptr, &_state->chunk_view);

    return;
}

void RenderResources(void* _pstate,
                     vktk::Context* _vulkan,
                     vktk::Swapchain const& _swapchain)
{
    state_t* _state = (state_t*)_pstate;


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
        gl_Position = vec4(quad, 0.0, 1.0);
        texCoords = quad * 0.5 + vec2(0.5);
    }
    )_";
    static uint32_t const vsize = sizeof(vshader) - 1u;

    static char const fshader[] = R"(
    #version 450
    layout(location = 0) in vec2 texCoords;
    layout(location = 0) out vec4 color;
    layout(set = 0, binding = 0) uniform sampler3D chunk;
    void main() {
        float fontv = texture(chunk, vec3(texCoords, 0.0)).x;
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

bool Update(void*, vktk::Context*, iotk::input_t const*, iotk::input_t const*)
{
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
    image_info.imageView = _state->chunk_view;
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

    vkCmdDraw(command_buffer, 4u, 1u, 0u, 0u);

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
    vkDestroyImage(_vulkan->device, _state->chunk.image, nullptr);
}

}

namespace Voxels
{

Interface::Interface()
    : ConstantResources{ ::ConstantResources }
    , RenderResources{ ::RenderResources }
    , Update{ ::Update }
    , Draw{ ::Draw }
    , Shutdown{ ::Shutdown }
    , state{ new state_t{} }
{}

Interface::~Interface()
{
    delete (state_t*)state;
}

}
