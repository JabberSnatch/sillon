#include "_voxels.hh"

#define ZIPTK_IMPLEMENTATION
#define MCTK_IMPLEMENTATION
#include "mctk/mctk.hh"

#include "systk.hh"
#include "numtk.hh"
#include "vktk.hh"

#include <memory>

namespace {

struct RegionData
{
    struct SectionData
    {
        std::vector<std::string> palette;
        std::array<uint16_t, 4096> block_ids;
    };

    struct ChunkData
    {
        std::string status;
        std::array<SectionData, 24> sections;
    };

    std::vector<ChunkData> chunks;
};

struct TurntableCamera
{
    numtk::vec3f spherical_coordinates = { 0.f, 0.f, 3.f };
    float y_offset = 0.f;
    numtk::vec3<int32_t> current_section = { 16, 8, 16 };

    void Update(iotk::input_t const* _input,
                iotk::input_t const* _past_input);
    numtk::mat4f Matrix() const;
};

struct FlightCamera
{
    numtk::vec2f forward_spherical = { 0.f, 0.f };
    numtk::vec3f position = { 0.f, 0.f, -3.f };
    float velocity = 5.f;

    void Update(iotk::input_t const* _input,
                iotk::input_t const* _past_input);
    numtk::mat4f Matrix() const;
};

struct state_t
{
    float time = 0.f;

    //TurntableCamera camera;
    FlightCamera camera;

    vktk::Texture visibility_texture;

    VkPipeline region_pipeline;
    VkPipelineLayout region_pipeline_layout;
    vktk::DescriptorPool descriptor_pool;
    VkDescriptorSet descriptor_set[64];

    VkFence last_submit;

    vktk::Buffer matrix_buffer;
    vktk::Buffer staging_buffer;
    void* staging_area;

    VkSampler sampler;
    vktk::Texture region_texture;
    RegionData region;

    uint32_t section_index = 0;
    std::vector<vktk::Texture> section_textures{};
    vktk::Texture culling_volume{};
};

struct MatrixBlock
{
    numtk::mat4f projection;
    numtk::mat4f view_model;
    numtk::mat4f view;
    numtk::vec2<int32_t> screen_size;
    float padding[14];
};

RegionData::ChunkData DecodeChunk(mctk::MCAChunkData const& chunk_data)
{
    using namespace mctk;

    RegionData::ChunkData output = {};

    std::vector<NBTField> chunk_fields = MCAPackedNBTDecode(chunk_data);
    if (chunk_fields.empty())
        return output;

    NBTCompound const& root = *(NBTCompound const*)chunk_fields[0].payload;
    for (NBTField const& field0 : root)
    {
        if (field0.name == "Status"
            && field0.type == TAG_String)
            output.status = *(NBTString const*)field0.payload;

        if (field0.name == "sections"
            && field0.type == TAG_List)
        {
            NBTList const& sections = *(NBTList const*)field0.payload;
            NBTCompound const* compounds = (NBTCompound const*)sections.data;
            for (uint32_t index = 0u; index < sections.size; ++index)
            {
                NBTCompound const& compound = compounds[index];
                int32_t section_index = -1;
                RegionData::SectionData section_data = {};
                for (NBTField const& field1 : compound)
                {
                    if (field1.name == "Y"
                        && field1.type == TAG_Byte)
                        section_index = (*(int8_t const*)field1.payload) + 4;

                    if (field1.name == "block_states"
                        && field1.type == TAG_Compound)
                    {
                        NBTCompound const& block_states = *(NBTCompound const*)field1.payload;
                        NBTLongArray const* block_data_field = nullptr;
                        uint32_t palette_size = 0u;
                        std::vector<std::string>& palette_data = section_data.palette;

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
                                        if (field3.name == "Name"
                                            && field3.type == TAG_String)
                                            palette_data.push_back(
                                                (*(NBTString const*)field3.payload).substr(10));
                                }
                            }

                            if (field2.name == "data"
                                && field2.type == TAG_Long_Array)
                                block_data_field = (NBTLongArray const*)field2.payload;
                        }

                        if (palette_size == 1)
                        {
                            std::memset(section_data.block_ids.data(), 0, 2 * 4096);
                            continue;
                        }

                        NBTLongArray const& block_data = *block_data_field;
                        uint32_t bit_count = numtk::log2(palette_size-1);
                        if (palette_size - (1 << (bit_count-1)))
                            ++bit_count;
                        bit_count = std::max(bit_count, 4u);

                        uint64_t block_index = 0;
                        uint32_t data_index = 0;
                        uint8_t data_offset = 0;
                        while (block_index < 4096 && data_index < block_data.size())
                        {
                            if (data_offset + bit_count > 64)
                            {
                                data_offset = 0;
                                ++data_index;
                            }

                            uint16_t block_id = (uint16_t)
                                ((block_data[data_index] >> data_offset)
                                 & ((1ull << bit_count) - 1));

                            uint32_t x = block_index % 16;
                            uint32_t z = (block_index / 16) % 16;
                            uint32_t y = (block_index / 256) % 16;

                            section_data.block_ids[z * 256 + y * 16 + x] = block_id;
                            data_offset += bit_count;
                            ++block_index;
                        }
                    }
                }

                if (section_index >= 0)
                    output.sections[section_index] = std::move(section_data);
            }
        }
    }

    return output;
}

RegionData DeserialiseRegion(std::string const& file_path)
{
    systk::Timer scope_time{ "DeserialiseRegion",
        [](std::string const& tag, float time) {
            std::cout << "[" << tag << "] " << time << " s" << std::endl;
        }
    };

    std::vector<uint8_t> region_file = systk::LoadFile(file_path.c_str());
    mctk::MCAFileData mca_data = mctk::MCADecode(region_file.data(), region_file.size());
    RegionData output = {};
    output.chunks.reserve(1024);

    for (uint32_t index = 0; index < 1024; ++index)
        output.chunks.emplace_back(DecodeChunk(mca_data.chunks[index]));

    return output;
}

void LoadSection(state_t* state, vktk::Context* vulkan, vktk::CommandBuffer& update_commands,
                 numtk::vec3<int32_t> const& coords)
{
    uint32_t chunk_index = coords[0] + coords[2]*32;
    RegionData::ChunkData const& chunk = state->region.chunks[chunk_index];
    RegionData::SectionData const& section = chunk.sections[coords[1]];

    std::vector<std::unique_ptr<uint8_t[]>> mip_levels = {};

    if (section.palette.size() > 1)
    {
        state->section_textures.push_back(
            vulkan->CreateTexture(16, 16, 16, 5,
                                  VK_IMAGE_TYPE_3D, VK_FORMAT_R8_UNORM,
                                  VK_IMAGE_USAGE_TRANSFER_DST_BIT
                                  | VK_IMAGE_USAGE_SAMPLED_BIT));

        update_commands.ImageTransitions(
            VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            {
                {
                    state->section_textures.back().image,
                    VK_ACCESS_NONE, VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_IMAGE_ASPECT_COLOR_BIT,
                }
            }
        );

        mip_levels.emplace_back(new uint8_t[4096]);
        {
            uint8_t* blocks = mip_levels.back().get();
            for (uint32_t index = 0u; index < 4096; ++index)
            {
                blocks[index] =
                    (!section.palette.empty()
                     && (section.palette[section.block_ids[index]] != "air")
                     && (section.palette[section.block_ids[index]] != "glow_lichen"))
                    ? 255
                    : 0;
            }

            update_commands.UpdateTexture(
                state->section_textures.back(), { 0, 0, 0 }, { 16, 16, 16 }, 0,
                [blocks](void* mapped_data) {
                    std::memcpy(mapped_data, blocks, 4096);
                }
            );
        }

        for (uint32_t mip_index = 0; mip_index < 4; ++mip_index)
        {
            uint32_t src_mip_size = 16 >> mip_index;
            uint32_t dst_mip_size = 8 >> mip_index;
            uint32_t block_count = 4096 / (8 << (3*mip_index));
            mip_levels.emplace_back(new uint8_t[block_count]);

            uint8_t* src = mip_levels[mip_index].get();
            uint8_t* dst = mip_levels[mip_index+1].get();
            for (uint32_t index = 0u; index < block_count; ++index)
            {
                uint32_t x = index % dst_mip_size;
                uint32_t y = (index / dst_mip_size) % dst_mip_size;
                uint32_t z = (index / (dst_mip_size*dst_mip_size));

                uint8_t v = 0;
                //uint8_t v = 255;
                for (uint32_t sample = 0u; sample < 8; ++sample)
                {
                    uint32_t sample_x = x*2 + (sample&1);
                    uint32_t sample_y = y*2 + ((sample>>1)&1);
                    uint32_t sample_z = z*2 + ((sample>>2)&1);
                    uint32_t input_index =
                        sample_x
                        + sample_y * src_mip_size
                        + sample_z * src_mip_size * src_mip_size;

                    uint8_t block = src[input_index];
                    v = std::max(v, block);
                    //v = std::min(v, block);
                }
                dst[index] = v;
            }

            update_commands.UpdateTexture(
                state->section_textures.back(),
                { 0, 0, 0 }, { dst_mip_size, dst_mip_size, dst_mip_size },
                mip_index + 1,
                [dst, block_count](void* mapped_data) {
                    std::memcpy(mapped_data, dst, block_count);
                }
            );
        }

        update_commands.ImageTransitions(
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            {
                {
                    state->section_textures.back().image,
                    VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_IMAGE_ASPECT_COLOR_BIT,
                }
            }
        );
    }

    update_commands.ImageTransitions(
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        {
            {
                state->region_texture.image,
                VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_ASPECT_COLOR_BIT,
            }
        });

    if (state->section_textures.size() > state->section_index)
    {
        update_commands.UpdateTexture(
            state->region_texture,
            VkOffset3D{ coords[0], coords[1], coords[2] },
            VkExtent3D{ 1, 1, 1 },
            [section_index = state->section_index,
             &mip_levels](void* mapped_data) {
                uint16_t* section_info = (uint16_t*)mapped_data;
#if 1
                section_info[1] = (uint16_t)section_index;
#else
                if (mip_levels[4][0])
                    section_info[1] = 0x80ff;
                else
                    section_info[1] = 0x8000;
#endif
            });

        state->section_index++;
    }
    else
    {
        update_commands.UpdateTexture(
            state->region_texture,
            VkOffset3D{ coords[0], coords[1], coords[2] },
            VkExtent3D{ 1, 1, 1 },
            [&section](void* mapped_data) {
                uint16_t* section_info = (uint16_t*)mapped_data;
                if (section.palette.empty()
                    || section.palette[0] != "air"
                    || section.palette[0] != "glow_lichen")
                    section_info[1] = 0x8000;
                else
                    section_info[1] = 0x80ff;
            });
    }

    update_commands.ImageTransitions(
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        {
            {
                state->region_texture.image,
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_IMAGE_ASPECT_COLOR_BIT,
            }
        });
}

void LoadArea(state_t* _state, vktk::Context* _vulkan,
              numtk::vec3<int32_t> const& _begin,
              numtk::vec3<int32_t> const& _end)
{
    systk::Timer scope_time{ "LoadArea",
        [](std::string const& tag, float time) {
            std::cout << "[" << tag << "] " << time << " s" << std::endl;
        }
    };

    vktk::CommandBuffer update_commands = _vulkan->CreateCommandBuffer(_vulkan->present_queue);
    update_commands.Begin();

    uint32_t sync_counter = 0;
    for (int32_t x = _begin.x(); x < _end.x(); ++x)
        for (int32_t y = _begin.y(); y < _end.y(); ++y)
            for (int32_t z = _begin.z(); z < _end.z(); ++z)
                LoadSection(_state, _vulkan, update_commands, { x, y, z });

    update_commands.End();
    update_commands.Submit();
}

void LoadRegion(state_t* state, vktk::Context* vulkan)
{
    systk::Timer scope_time{ "LoadRegion",
        [](std::string const& tag, float time) {
            std::cout << "[" << tag << "] " << time << " s" << std::endl;
        }
    };

    RegionData const& region = state->region;
    state->region_texture = vulkan->CreateTexture(
        32, 32, 32,
        VK_IMAGE_TYPE_3D, VK_FORMAT_R16G16_UINT,
        VK_IMAGE_USAGE_SAMPLED_BIT,
        [&region](void* mapped_data) {
            uint16_t* sections = (uint16_t*)mapped_data;

            for (uint32_t xindex = 0; xindex < 32; ++xindex)
            {
                for (uint32_t zindex = 0; zindex < 32; ++zindex)
                {
                    RegionData::ChunkData const& chunk_data = region.chunks[xindex + zindex*32];
                    for (uint32_t yindex = 0; yindex < 32; ++yindex)
                    {
                        uint32_t index = xindex + yindex*32 + zindex*32*32;
                        RegionData::SectionData const& section = chunk_data.sections[yindex];
                        sections[index * 2+1] = 0xffff;
                        if (yindex < 24)
                        {
                            float average = 0.f;

                            if (!section.palette.empty())
                                for (uint16_t block_id : section.block_ids)
                                    average +=
                                        ((section.palette[block_id] != "air")
                                         && (section.palette[block_id] != "glow_lichen"))
                                        ? 1.f / 4096.f
                                        : 0.f;

                            sections[index * 2] = (uint16_t)(average * 65535.f);
                        }
                        else
                            sections[index * 2] = 0;
                    }
                }
            }
        });
}

void ConstantResources(void* _pstate, vktk::Context* _vulkan)
{
    state_t* _state = (state_t*)_pstate;

    _state->region = DeserialiseRegion("../r.-1.6.mca");
    LoadRegion(_state, _vulkan);
    LoadArea(_state, _vulkan, { 0, 0, 0 }, { 32, 16, 32 });

    // 8 * 32 * 16 = 4096
    // 8 * 32 * 4 = 1024
    // r32g32 = 64b
    // 16^3 = 4096
    // 64^3 = 262144
    // 64^3 * 4096 = 1073741824
    // 1024^3 = 1073741824
    _state->culling_volume = _vulkan->CreateTexture(
        8, 8, 8,
        VK_IMAGE_TYPE_3D, VK_FORMAT_R32G32_UINT,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);

    // +--+--+--+
    // |OO|XX|  |
    // +--+--+--+
    // |  |XX|XX|>
    // +--+--+--+
    // |  |  |  |
    // +--+--+--+
    // 110 011 000

    _state->descriptor_pool = _vulkan->CreateDescriptorPool(64u, {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
          0, VK_SHADER_STAGE_FRAGMENT_BIT, 1 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
          0, VK_SHADER_STAGE_ALL, 1 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
          VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT,
          VK_SHADER_STAGE_FRAGMENT_BIT, 32 * 32 * 32 }
    });

    uint32_t desc_count = (uint32_t)_state->section_textures.size();
    VkDescriptorSetVariableDescriptorCountAllocateInfo desc_count_alloc = {};
    desc_count_alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO;
    desc_count_alloc.pNext = nullptr;
    desc_count_alloc.descriptorSetCount = 1;
    desc_count_alloc.pDescriptorCounts = &desc_count;

    VkDescriptorSetAllocateInfo descset_allocate{};
    descset_allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    descset_allocate.pNext= &desc_count_alloc;
    descset_allocate.descriptorPool = _state->descriptor_pool.pool;
    descset_allocate.descriptorSetCount = 1u;
    descset_allocate.pSetLayouts = &_state->descriptor_pool.layout;
    for (uint32_t index = 0; index < 64; ++index)
        CHECKCALL(vkAllocateDescriptorSets, _vulkan->device,
                  &descset_allocate, &_state->descriptor_set[index]);

    VkPipelineLayoutCreateInfo pipeline_create{};
    pipeline_create.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_create.pNext = nullptr;
    pipeline_create.flags = 0u;
    pipeline_create.setLayoutCount = 1u;
    pipeline_create.pSetLayouts = &_state->descriptor_pool.layout;
    pipeline_create.pushConstantRangeCount = 0;
    pipeline_create.pPushConstantRanges = nullptr;
    CHECKCALL(vkCreatePipelineLayout, _vulkan->device, &pipeline_create, nullptr,
              &_state->region_pipeline_layout);

    _state->staging_buffer = _vulkan->CreateBuffer(
        sizeof(MatrixBlock) * 64,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
        | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    _state->matrix_buffer = _vulkan->CreateBuffer(
        sizeof(MatrixBlock) * 64,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT
        | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);

    _state->staging_area = nullptr;
    CHECKCALL(vkMapMemory,
              _vulkan->device, _state->staging_buffer.alloc.memory,
              _state->staging_buffer.alloc.offset, _state->staging_buffer.alloc.size,
              0, &_state->staging_area);

    MatrixBlock* matrices = (MatrixBlock*)_state->staging_area;
    for (uint32_t index = 0; index < 64; ++index)
    {
        matrices[index].projection = numtk::mat4f::Constant(0.f);
        matrices[index].view_model = numtk::mat4f::Constant(0.f);
        matrices[index].view = numtk::mat4f::Constant(0.f);
        matrices[index].screen_size = numtk::vec2<int32_t>::Constant(1);
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
    sampler_create.maxLod = 4.0;
    CHECKCALL(vkCreateSampler, _vulkan->device,
              &sampler_create, nullptr, &_state->sampler);

    VkDescriptorImageInfo region_image_info{};
    region_image_info.sampler = _state->sampler;
    region_image_info.imageView = _state->region_texture.view;
    region_image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    for (uint32_t index = 0; index < 64; ++index)
    {
        {
            VkWriteDescriptorSet desc_writes[2];
            {
                VkWriteDescriptorSet& desc_write = desc_writes[0];
                desc_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                desc_write.pNext = nullptr;
                desc_write.dstSet = _state->descriptor_set[index];
                desc_write.dstBinding = 0u;
                desc_write.dstArrayElement = 0u;
                desc_write.descriptorCount = 1u;
                desc_write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                desc_write.pImageInfo = &region_image_info;
                desc_write.pBufferInfo = nullptr;
                desc_write.pTexelBufferView = nullptr;
            }


            VkDescriptorBufferInfo buffer_info = {};
            buffer_info.buffer = _state->matrix_buffer;
            buffer_info.offset = sizeof(MatrixBlock) * index;
            buffer_info.range = sizeof(MatrixBlock);

            {
                VkWriteDescriptorSet& desc_write = desc_writes[1];
                desc_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                desc_write.pNext = nullptr;
                desc_write.dstSet = _state->descriptor_set[index];
                desc_write.dstBinding = 1u;
                desc_write.dstArrayElement = 0u;
                desc_write.descriptorCount = 1u;
                desc_write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                desc_write.pImageInfo = nullptr;
                desc_write.pBufferInfo = &buffer_info;
                desc_write.pTexelBufferView = nullptr;
            }

            vkUpdateDescriptorSets(_vulkan->device, 2u, desc_writes, 0u, nullptr);
        }

        {
            std::vector<VkDescriptorImageInfo> section_images_info(_state->section_textures.size());
            for (uint32_t section_index = 0u;
                 section_index < _state->section_textures.size();
                 ++section_index)
            {
                VkDescriptorImageInfo& image_info = section_images_info[section_index];
                image_info.sampler = _state->sampler;
                image_info.imageView = _state->section_textures[section_index].view;
                image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            }

            VkWriteDescriptorSet desc_write = {};
            desc_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            desc_write.pNext = nullptr;
            desc_write.dstSet = _state->descriptor_set[index];
            desc_write.dstBinding = 2u;
            desc_write.dstArrayElement = 0u;
            desc_write.descriptorCount = (uint32_t)_state->section_textures.size();
            desc_write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            desc_write.pImageInfo = section_images_info.data();
            desc_write.pBufferInfo = nullptr;
            desc_write.pTexelBufferView = nullptr;

            vkUpdateDescriptorSets(_vulkan->device, 1u, &desc_write, 0u, nullptr);
        }
    }

    vktk::CommandBuffer command_buffer = _vulkan->CreateCommandBuffer(_vulkan->present_queue);
    command_buffer.Begin();

    command_buffer.BufferTransitions(
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        {
            { _state->matrix_buffer,
              VK_ACCESS_NONE,
              VK_ACCESS_TRANSFER_WRITE_BIT },
        });

    command_buffer.BufferCopy(_state->matrix_buffer, 0, _state->staging_buffer, 0,
                              sizeof(MatrixBlock) * 64);

    command_buffer.BufferTransitions(
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
        {
            { _state->matrix_buffer,
              VK_ACCESS_TRANSFER_WRITE_BIT,
              VK_ACCESS_UNIFORM_READ_BIT }
        });

    command_buffer.End();

    _vulkan->present_queue.Submit(
        &command_buffer.handle, 1u
    );

    return;
}

void RenderResources(void* _pstate,
                     vktk::Context* _vulkan,
                     vktk::Swapchain const& _swapchain)
{
    state_t* _state = (state_t*)_pstate;

    constexpr uint32_t kStageCount = 2u;

    //   2---6
    //  /   /|
    // 3---7 |
    // | 0 | 4
    // |   |/
    // 1---5

    //   Y
    //   |
    //   o-X
    //  /
    // Z

    static char const draw_cube_vshader[] = R"_(
    const uint kTriIndices[] = uint[36](
#ifndef CW_CUBE
// COUNTERCLOCKWISE FACES
        0, 1, 2,  2, 1, 3,
        0, 4, 1,  1, 4, 5,
        3, 1, 7,  7, 1, 5,
        7, 5, 6,  6, 5, 4,
        2, 6, 0,  0, 6, 4,
        2, 3, 6,  6, 3, 7
#else
// CLOCKWISE FACES
        0, 2, 1,  1, 2, 3,
        0, 1, 4,  4, 1, 5,
        3, 7, 1,  1, 7, 5,
        7, 6, 5,  5, 6, 4,
        2, 0, 6,  6, 0, 4,
        2, 6, 3,  3, 6, 7
#endif
    );

    const vec3 kTriVertices[] = vec3[8](
        vec3(-1.0, -1.0, -1.0),
        vec3(-1.0, -1.0, 1.0),
        vec3(-1.0, 1.0, -1.0),
        vec3(-1.0, 1.0, 1.0),
        vec3(1.0, -1.0, -1.0),
        vec3(1.0, -1.0, 1.0),
        vec3(1.0, 1.0, -1.0),
        vec3(1.0, 1.0, 1.0)
    );

    layout(std140, set = 0, binding = 1) uniform MatrixBlock {
        mat4 projection;
        mat4 view_model;
        mat4 view;
        ivec2 screen_size;
    };

    layout(location = 0) out vec3 iFS_volume_coords;
    layout(location = 1) out vec3 iFS_camera_position;

    void main() {
        vec3 vertex = kTriVertices[kTriIndices[gl_VertexIndex]];
        gl_Position = projection * view_model * vec4(vertex, 1.0);

        iFS_camera_position = (inverse(view_model) * vec4(vec3(0.0), 1.0)).xyz;
        iFS_volume_coords = vertex * 0.5 + vec3(0.5);
    }
    )_";

    static char const voxel_traversal_func[] = R"(
    struct DDAContext
    {
        vec3 step_sign;
        vec3 exit_value;
        vec3 delta_t;
        vec3 max_t;
        vec3 voxel_coords;
        vec3 voxel_step;
        vec4 mask;
        float t;
    };

    DDAContext InitDDAContext(vec3 org, vec3 dir, float voxel_count)
    {
        vec3 voxel_space_org = clamp(org, vec3(0.0), vec3(0.99999)) * voxel_count;

        DDAContext ctx;

        ctx.step_sign = sign(dir);
        ctx.exit_value = ctx.step_sign * 0.5 + vec3(0.5);
        vec3 edge_distance = ctx.exit_value - (ctx.step_sign * fract(voxel_space_org));
        ctx.delta_t = 1.0 / (abs(dir) * voxel_count);
        ctx.max_t = abs(edge_distance) * ctx.delta_t;
        ctx.voxel_coords = (floor(voxel_space_org) + vec3(0.5)) / voxel_count;
        ctx.voxel_step = ctx.step_sign / voxel_count;

        ctx.mask.xyz = vec3(0.0);
        // Equivalent to mask =
        // ((voxel_coords.x * step_sign.x < exit_value.x) &&
        // (voxel_coords.y * step_sign.y < exit_value.y) &&
        // (voxel_coords.z * step_sign.z < exit_value.z)) ? 1.0 : 0.0
        ctx.mask.w =
            (1.0 - step(ctx.exit_value.x, ctx.voxel_coords.x * ctx.step_sign.x))
            * (1.0 - step(ctx.exit_value.y, ctx.voxel_coords.y * ctx.step_sign.y))
            * (1.0 - step(ctx.exit_value.z, ctx.voxel_coords.z * ctx.step_sign.z));

        ctx.t = 0.0;

        return ctx;
    }

    void StepDDAContext(inout DDAContext ctx)
    {
#if 0
        ctx.mask.w =
            (1.0 - step(ctx.exit_value.x, ctx.voxel_coords.x * ctx.step_sign.x))
            * (1.0 - step(ctx.exit_value.y, ctx.voxel_coords.y * ctx.step_sign.y))
            * (1.0 - step(ctx.exit_value.z, ctx.voxel_coords.z * ctx.step_sign.z));
#endif

        vec3 order = vec3(
            // ctx.max_t.x < ctx.max_t.y
            1.0 - step(ctx.max_t.y, ctx.max_t.x),
            // ctx.max_t.x < ctx.max_t.z
            1.0 - step(ctx.max_t.z, ctx.max_t.x),
            // ctx.max_t.y < ctx.max_t.z
            1.0 - step(ctx.max_t.z, ctx.max_t.y));

        ctx.mask.xyz = vec3(
            // X && Y
            order.x * order.y,
            // !X && Z
            (1.0 - order.x) * order.z,
            // (X && !Y) || (!X && Z)
            order.x * (1.0 - order.y) + (1.0 - order.x) * (1.0 - order.z)
        );

        ctx.t = min(ctx.max_t.x, min(ctx.max_t.y, ctx.max_t.z));
        ctx.voxel_coords += ctx.voxel_step * ctx.mask.xyz * ctx.mask.w;
        ctx.max_t += ctx.delta_t * ctx.mask.xyz * ctx.mask.w;

        ctx.mask.w =
            (1.0 - step(ctx.exit_value.x, ctx.voxel_coords.x * ctx.step_sign.x))
            * (1.0 - step(ctx.exit_value.y, ctx.voxel_coords.y * ctx.step_sign.y))
            * (1.0 - step(ctx.exit_value.z, ctx.voxel_coords.z * ctx.step_sign.z));
    }
    )";

    static char const region_fshader[] =
        #include "shaders/render_region.frag.h"
        ;

    {
        VkShaderModule vmodule =
            _vulkan->CompileShader_GLSL(VK_SHADER_STAGE_VERTEX_BIT,
                                        "draw_cube_vshader_cw",
                                        {
                                            "#version 450",
                                            "#define CW_CUBE",
                                            draw_cube_vshader
                                        });

        VkShaderModule fmodule =
            _vulkan->CompileShader_GLSL(VK_SHADER_STAGE_FRAGMENT_BIT,
                                        "region_fshader",
                                        {
                                            "#version 450",
                                            voxel_traversal_func,
                                            region_fshader
                                        });

        if (_state->region_pipeline != VK_NULL_HANDLE)
            vkDestroyPipeline(_vulkan->device, _state->region_pipeline, nullptr);

        _state->region_pipeline = _vulkan->CreatePipeline(
            _state->region_pipeline_layout,
            _swapchain.render_pass,
            {
                { vmodule, VK_SHADER_STAGE_VERTEX_BIT },
                { fmodule, VK_SHADER_STAGE_FRAGMENT_BIT }
            },
            VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            {}
        );

        vkDestroyShaderModule(_vulkan->device, vmodule, nullptr);
        vkDestroyShaderModule(_vulkan->device, fmodule, nullptr);
    }

    if (_state->visibility_texture.image != VK_NULL_HANDLE)
        _vulkan->DestroyTexture(_state->visibility_texture);

    _vulkan->CreateTexture(_swapchain.size.height,
                           _swapchain.size.width,
                           1,
                           VK_IMAGE_TYPE_2D, VK_FORMAT_D16_UNORM,
                           VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
                           | VK_IMAGE_USAGE_SAMPLED_BIT);
}

bool Update(void* _pstate, vktk::Context* _vulkan,
            iotk::input_t const* _input,
            iotk::input_t const* _past_input)
{
    state_t* _state = (state_t*)_pstate;

    std::cout << _input->time_delta << std::endl;
    _state->time += _input->time_delta;

    if (_input->key_down[iotk::eKey::kEscape])
        return false;

    _state->camera.Update(_input, _past_input);

    return true;
}

void Draw(void* _pstate,
          vktk::Context* _vulkan,
          vktk::Swapchain const& _swapchain,
          VkSemaphore _acquire_semaphore)
{
    state_t* _state = (state_t*)_pstate;

    float how = (float)_swapchain.size.height / (float)_swapchain.size.width;

    MatrixBlock* matrices = (MatrixBlock*)_state->staging_area;
    for (uint32_t index = 0; index < 64; ++index)
    {
        matrices[index].projection =
            numtk::mat4f::Perspective_VulkanNDC(0.1f, 100.f, 90.f*numtk::kDegrees, how);
        matrices[index].view = _state->camera.Matrix();
        matrices[index].view_model =
            matrices[index].view
            * numtk::mat4f{
            2.f, 0.f, 0.f, 0.f,
            0.f, 2.f, 0.f, 0.f,
            0.f, 0.f, 2.f, 0.f,
            0.f, 0.f, 0.f, 1.f}
            * numtk::mat4f{
            1.f, 0.f, 0.f, 0.f,
            0.f, 1.f, 0.f, 0.f,
            0.f, 0.f, 1.f, 0.f,
            (float)(index/8) * 2.f, 0.f, (float)(index%8) * 2.f, 1.f};

        matrices[index].screen_size = {
            (int32_t)_swapchain.size.width,
            (int32_t)_swapchain.size.height
        };
    }

    vktk::CommandBuffer command_buffer = _vulkan->CreateCommandBuffer(_vulkan->present_queue);
    command_buffer.Begin();

    command_buffer.BufferTransitions(
        VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        {
            { _state->matrix_buffer, VK_ACCESS_UNIFORM_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT }
        });

    command_buffer.BufferCopy(_state->matrix_buffer, 0,
                              _state->staging_buffer, 0,
                              sizeof(MatrixBlock) * 64);

    command_buffer.BufferTransitions(
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
        {
            { _state->matrix_buffer, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_UNIFORM_READ_BIT }
        });

    VkViewport viewport{};
    viewport.x = 0.f; viewport.y = 0.f;
    viewport.width = (float)_swapchain.size.width;
    viewport.height = (float)_swapchain.size.height;
    viewport.minDepth = 0.f; viewport.maxDepth = 1.f;
    vkCmdSetViewport(command_buffer, 0, 1, &viewport);

    VkRect2D scissor{ VkOffset2D{ 0, 0 }, _swapchain.size };
    vkCmdSetScissor(command_buffer, 0, 1, &scissor);

    VkClearValue clear_values[2];
    {
        VkClearValue& clear_value = clear_values[0];
        clear_value.color.float32[0] = 0.f; clear_value.color.float32[1] = 1.f;
        clear_value.color.float32[2] = 1.f; clear_value.color.float32[3] = 0.f;
        clear_value.depthStencil.depth = 0.f;
        clear_value.depthStencil.stencil = 0;
    }
    {
        VkClearValue& clear_value = clear_values[1];
        clear_value.color.float32[0] = 0.f; clear_value.color.float32[1] = 0.f;
        clear_value.color.float32[2] = 0.f; clear_value.color.float32[3] = 0.f;
        clear_value.depthStencil.depth = 1.f;
        clear_value.depthStencil.stencil = 0;
    }

    VkRenderPassBeginInfo rp_begin_info{};
    rp_begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp_begin_info.pNext = nullptr;
    rp_begin_info.renderPass = _swapchain.render_pass;
    rp_begin_info.framebuffer = _swapchain.framebuffers[_swapchain.index];
    rp_begin_info.renderArea = VkRect2D{
        VkOffset2D{ 0, 0 },
        _swapchain.size
    };
    rp_begin_info.clearValueCount = 2;
    rp_begin_info.pClearValues = clear_values;

    vkCmdBeginRenderPass(command_buffer, &rp_begin_info, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _state->region_pipeline);
    for (uint32_t index = 0; index < 64; ++index)
    {

        vkCmdBindDescriptorSets(command_buffer,
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                _state->region_pipeline_layout, 0u,
                                1u, &_state->descriptor_set[index],
                                0u, nullptr);

        vkCmdDraw(command_buffer, 36u, 1u, 0u, 0u);

    }
    vkCmdEndRenderPass(command_buffer);

    command_buffer.End();

    if (_state->last_submit != VK_NULL_HANDLE)
        _vulkan->ScheduleFenceDestroy(_state->last_submit);

    VkPipelineStageFlags wait_dst_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    command_buffer.Submit(nullptr, &_acquire_semaphore, &wait_dst_stage, 1u,
                          &_state->last_submit);
#if 0
    std::cout << "[VoxelsDraw] submitted on fence "
              << std::hex << _state->last_submit << std::dec << std::endl;
#endif

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
    vkDestroyPipeline(_vulkan->device, _state->region_pipeline, nullptr);
    _state->region_pipeline = VK_NULL_HANDLE;
    vkDestroyPipelineLayout(_vulkan->device, _state->region_pipeline_layout, nullptr);
    _state->region_pipeline_layout = VK_NULL_HANDLE;
    vkDestroySampler(_vulkan->device, _state->sampler, nullptr);
    _state->sampler = VK_NULL_HANDLE;

    vkFreeDescriptorSets(_vulkan->device, _state->descriptor_pool.pool, 64, _state->descriptor_set);
    for (uint32_t index = 0; index < 64; ++index)
        _state->descriptor_set[index] = VK_NULL_HANDLE;
    _vulkan->DestroyDescriptorPool(_state->descriptor_pool);

    vkDestroyImage(_vulkan->device, _state->region_texture.image, nullptr);
    _state->region_texture.image = VK_NULL_HANDLE;
    vkFreeMemory(_vulkan->device, _state->region_texture.memory, nullptr);
    _state->region_texture.memory = VK_NULL_HANDLE;

    vkUnmapMemory(_vulkan->device, _state->staging_buffer.alloc.memory);

    _vulkan->DestroyBuffer(_state->matrix_buffer);
    _vulkan->DestroyBuffer(_state->staging_buffer);

    for (uint32_t index = 0; index < _state->section_textures.size(); ++index)
        _vulkan->DestroyTexture(_state->section_textures[index]);

    if (_state->last_submit != VK_NULL_HANDLE)
    {
        _vulkan->ScheduleFenceDestroy(_state->last_submit);
        _state->last_submit = VK_NULL_HANDLE;
    }
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
    }

    {
        spherical_coordinates.z() *= (1.f + 0.0005f * (float)_input->wheel_delta);
        if (spherical_coordinates.z() < 0.01f)
            spherical_coordinates.z() = 0.01f;
    }

    if (_input->button_down & iotk::fMouseButton::kRightBtn)
    {
        y_offset += 0.01f * (float)(_input->cursor[1] -  _past_input->cursor[1]);
    }

    {
        numtk::vec3<int32_t> next_section = current_section;
        #define KEY_PRESSED(key) _input->key_down[key] && !_past_input->key_down[key]
        if (KEY_PRESSED('w'))
            ++next_section[0];
        if (KEY_PRESSED('s'))
            --next_section[0];
        if (KEY_PRESSED('d'))
            ++next_section[2];
        if (KEY_PRESSED('a'))
            --next_section[2];
        if (KEY_PRESSED('q'))
            --next_section[1];
        if (KEY_PRESSED('e'))
            ++next_section[1];
        #undef KEY_PRESSED

        next_section.x() = std::max(0, std::min(31, next_section.x()));
        next_section.y() = std::max(0, std::min(23, next_section.y()));
        next_section.z() = std::max(0, std::min(31, next_section.z()));

        current_section = next_section;
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

    numtk::vec3f center = (numtk::vec3f{
        (float)current_section.x() + 0.5f,
        (float)current_section.y() + 0.5f,
        (float)current_section.z() + 0.5f
    } / 16.f) - numtk::vec3f{ 1.f, 1.f, 1.f } + numtk::vec3f{ 0.f, y_offset, 0.f };

    return
        numtk::mat4f::View({ 0.f, 0.f, -1.f },
                           { 0.f, 1.f, 0.f },
                           { 0.f, 0.f, spherical_coordinates.z() })
        * numtk::mat4f::View(forward, up, center);

}

void FlightCamera::Update(iotk::input_t const* _input,
                          iotk::input_t const* _past_input)
{
    {
        velocity *= (1.f + 0.005f * (float)_input->wheel_delta);
        if (velocity < 0.01f)
            velocity = 0.01f;
    }

    if (_input->button_down & iotk::fMouseButton::kLeftBtn)
    {
        numtk::vec2<int32_t> last_position{ _past_input->cursor[0], _past_input->cursor[1] };
        numtk::vec2<int32_t> position{ _input->cursor[0], _input->cursor[1] };
        numtk::vec2<int32_t> delta = position - last_position;
        forward_spherical += numtk::vec2f{
            0.01f * (float)delta.x(),
            -0.01f * (float)delta.y(),
        };

        forward_spherical.y() =
            std::min(std::max(forward_spherical.y(), -numtk::kPi*0.4999f), numtk::kPi*0.4999f);
    }

    numtk::vec3f forward = {
        std::cos(forward_spherical[1]) * std::cos(forward_spherical[0]),
        std::sin(forward_spherical[1]),
        std::cos(forward_spherical[1]) * std::sin(forward_spherical[0])
    };
    forward = forward.normalise();

    numtk::vec3f left = numtk::vec3f{ 0.f, 1.f, 0.f }.cross({
        std::cos(forward_spherical[0]),
        0.f,
        std::sin(forward_spherical[0])
    });

    numtk::vec3f movement = { 0.f, 0.f, 0.f };
    if (_input->key_down['w'])
        movement.z() += 1.f;
    if (_input->key_down['s'])
        movement.z() -= 1.f;
    if (_input->key_down['a'])
        movement.x() += 1.f;
    if (_input->key_down['d'])
        movement.x() -= 1.f;
    if (_input->key_down[' '])
        movement.y() += 1.f;
    if (_input->mod_down & iotk::fKeyMod::kShift)
        movement.y() -= 1.f;
    movement.normalise();

    position += (forward*movement.z() + left*movement.x() + numtk::vec3f{ 0.f, movement.y(), 0.f })
        * velocity * _input->time_delta;
}

numtk::mat4f FlightCamera::Matrix() const
{
    numtk::vec3f forward = {
        std::cos(forward_spherical[1]) * std::cos(forward_spherical[0]),
        std::sin(forward_spherical[1]),
        std::cos(forward_spherical[1]) * std::sin(forward_spherical[0])
    };
    forward = forward.normalise();

    numtk::vec3f up = { 0.f, 1.f, 0.f };
    if (std::abs(1.f - forward.dot(numtk::vec3f{ 0.f, 1.f, 0.f })) > 1e-3f)
        up = forward.cross(numtk::vec3f{ 0.f, 1.f, 0.f }).cross(forward).normalise();
    else
        up = forward.cross(numtk::vec3f{ 0.f, 0.f, 1.f }).cross(forward).normalise();

    return numtk::mat4f::View(forward, up, position);
}


}

namespace Voxels
{

Interface::Interface()
    : ConstantResources{}
    , RenderResources{}
    , Update{}
    , Draw{}
    , Shutdown{}
    , state{ new state_t{} }
{
    ReloadPointers();
}

Interface::~Interface()
{
    delete (state_t*)state;
}

void Interface::ReloadPointers()
{
    ConstantResources = ::ConstantResources;
    RenderResources = ::RenderResources;
    Update = ::Update;
    Draw = ::Draw;
    Shutdown = ::Shutdown;
}

}
