#include "_voxels.hh"

#define ZIPTK_IMPLEMENTATION
#define MCTK_IMPLEMENTATION
#include "mctk/mctk.hh"

#include "systk.hh"
#include "numtk.hh"
#include "vktk.hh"

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

struct state_t
{
    float time = 0.f;
    numtk::vec3f turntable = { 0.f, 0.f, 3.f };
    float y_offset = 0.f;
    bool updating_orientation = false;
    numtk::vec3<int32_t> current_section;

    VkPipeline region_pipeline;
    VkPipelineLayout pipeline_layout;
    VkSampler sampler;
    VkDescriptorPool descriptor_pool;
    VkDescriptorSet descriptor_set;
    VkDescriptorSetLayout descset_layout;

    VkFence last_submit;

    vktk::Buffer matrix_buffer;
    vktk::Buffer staging_buffer;
    void* staging_area;

    vktk::Texture chunk_texture;
    VkImageView chunk_view;
    vktk::Texture region_texture;
    VkImageView region_view;
    RegionData region;

    uint32_t section_index = 0;
    vktk::Texture section_textures[128];
    VkImageView section_views[128];
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
    std::vector<uint8_t> region_file = systk::LoadFile(file_path.c_str());
    mctk::MCAFileData mca_data = mctk::MCADecode(region_file.data(), region_file.size());
    RegionData output = {};
    output.chunks.reserve(1024);

    for (uint32_t index = 0; index < 1024; ++index)
        output.chunks.emplace_back(DecodeChunk(mca_data.chunks[index]));

    return output;
}

void LoadSection(state_t* state, vktk::Context* vulkan, numtk::vec3<int32_t> const& coords)
{
    if (state->section_index >= 128)
        return;

    uint32_t chunk_index = coords[0] + coords[2]*32;
    RegionData::ChunkData const& chunk = state->region.chunks[chunk_index];
    RegionData::SectionData const& section = chunk.sections[coords[1]];

    if (state->last_submit != VK_NULL_HANDLE
        && state->chunk_texture.image != VK_NULL_HANDLE)
        vulkan->ScheduleRoutine(state->last_submit,
                                [texture = state->chunk_texture](vktk::Context& _context) {
                                    _context.DestroyTexture(texture);
                                });

    state->section_textures[state->section_index] = vulkan->CreateTexture(
        16, 16, 16,
        VK_IMAGE_TYPE_3D, VK_FORMAT_R8_UNORM,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT
        | VK_IMAGE_USAGE_SAMPLED_BIT,
        [&section](void* mapped_data) {
            uint8_t* blocks = (uint8_t*)mapped_data;
            for (uint32_t index = 0u; index < 4096; ++index)
            {
                blocks[index] =
                    (!section.palette.empty()
                     && (section.palette[section.block_ids[index]] != "air")
                     && (section.palette[section.block_ids[index]] != "glow_lichen"))
                    ? 255
                    : 0;
            }
        });

    state->section_views[state->section_index] =
        vulkan->CreateImageView(state->section_textures[state->section_index]);

#if 1
    state->chunk_texture = vulkan->CreateTexture(
        16, 16, 16,
        VK_IMAGE_TYPE_3D, VK_FORMAT_R8_UNORM,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT
        | VK_IMAGE_USAGE_SAMPLED_BIT,
        [&section](void* mapped_data) {
            uint8_t* blocks = (uint8_t*)mapped_data;
            for (uint32_t index = 0u; index < 4096; ++index)
            {
                blocks[index] =
                    (!section.palette.empty()
                     && (section.palette[section.block_ids[index]] != "air")
                     && (section.palette[section.block_ids[index]] != "glow_lichen"))
                    ? 255
                    : 0;
            }
        });
#endif

    vktk::CommandBuffer update_commands = vulkan->CreateCommandBuffer(vulkan->present_queue);
    update_commands.Begin();

    update_commands.ImageTransitions(
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        {
            {
                state->region_texture.image,
                VK_ACCESS_SHADER_READ_BIT,
                VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_ASPECT_COLOR_BIT,
            }
        });

    update_commands.UpdateTexture(
        state->region_texture,
        VkOffset3D{ coords[0], coords[1], coords[2] },
        VkExtent3D{ 1, 1, 1 },
        [section_index = state->section_index](void* mapped_data) {
            uint16_t* section_info = (uint16_t*)mapped_data;
            section_info[1] = (uint16_t)section_index;
        });

#if 0
    update_commands.UpdateTexture(
        state->region_texture,
        VkOffset3D{ state->current_section[0], state->current_section[1], state->current_section[2] },
        VkExtent3D{ 1, 1, 1 },
        [](void* mapped_data) {
            uint16_t* section_info = (uint16_t*)mapped_data;
            section_info[1] = 0xffff;
        });
#endif

    update_commands.ImageTransitions(
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        {
            {
                state->region_texture.image,
                VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_ACCESS_SHADER_READ_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_IMAGE_ASPECT_COLOR_BIT,
            }
        });

    update_commands.End();
    update_commands.Submit();

    state->current_section = coords;
    state->section_index++;
}

void LoadRegion(state_t* state, vktk::Context* vulkan)
{
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
    LoadSection(_state, _vulkan, { 28, 6, 21 });

    VkDescriptorPoolSize descpool_sizes[2] = {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 130 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}
    };

    VkDescriptorPoolCreateInfo descpool_create{};
    descpool_create.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descpool_create.pNext = nullptr;
    descpool_create.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    descpool_create.maxSets = 1u;
    descpool_create.poolSizeCount = sizeof(descpool_sizes) / sizeof(descpool_sizes[0]);
    descpool_create.pPoolSizes = descpool_sizes;
    CHECKCALL(vkCreateDescriptorPool, _vulkan->device,
              &descpool_create, nullptr, &_state->descriptor_pool);

    VkDescriptorSetLayoutBinding bindings[4];
    {
        VkDescriptorSetLayoutBinding& binding = bindings[0];
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        binding.pImmutableSamplers = nullptr;
    }
    {
        VkDescriptorSetLayoutBinding& binding = bindings[1];
        binding.binding = 1;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_ALL;
        binding.pImmutableSamplers = nullptr;
    }
    {
        VkDescriptorSetLayoutBinding& binding = bindings[2];
        binding.binding = 2;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        binding.pImmutableSamplers = nullptr;
    }
    {
        VkDescriptorSetLayoutBinding& binding = bindings[3];
        binding.binding = 3;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = 128;
        binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        binding.pImmutableSamplers = nullptr;
    }

    VkDescriptorSetLayoutCreateInfo descset_create{};
    descset_create.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descset_create.pNext = nullptr;
    descset_create.flags = 0u;
    descset_create.bindingCount = 4;
    descset_create.pBindings = bindings;
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
    sampler_create.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_create.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_create.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
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

    _state->chunk_view = _vulkan->CreateImageView(_state->chunk_texture);
    _state->region_view = _vulkan->CreateImageView(_state->region_texture);

    _state->staging_buffer = _vulkan->CreateBuffer(
        sizeof(numtk::mat4f) * 3 + sizeof(numtk::vec2f),
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
        | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    _state->matrix_buffer = _vulkan->CreateBuffer(
        sizeof(numtk::mat4f) * 3 + sizeof(numtk::vec2f),
        VK_BUFFER_USAGE_TRANSFER_DST_BIT
        | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);

    _state->staging_area = nullptr;
    CHECKCALL(vkMapMemory,
              _vulkan->device, _state->staging_buffer.alloc.memory,
              _state->staging_buffer.alloc.offset, _state->staging_buffer.alloc.size,
              0, &_state->staging_area);

    numtk::mat4f* matrix = (numtk::mat4f*)_state->staging_area;
    matrix[0] = numtk::mat4f::Constant(0.f);
    matrix[1] = numtk::mat4f::Constant(0.f);
    matrix[2] = numtk::mat4f::Constant(0.f);
    ((int32_t*)(matrix + 3))[0] = 1;
    ((int32_t*)(matrix + 3))[1] = 1;

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
                              sizeof(numtk::mat4f) * 3 + sizeof(numtk::vec2f));

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
        vec3 edge_distance;
        vec3 delta_t;
        vec3 max_t;
        vec3 voxel_coords;
        vec3 voxel_step;
        vec4 mask;
    };

    DDAContext InitDDAContext(vec3 org, vec3 dir, float voxel_count)
    {
        vec3 voxel_space_org = clamp(org, vec3(0.0), vec3(0.99999)) * voxel_count;

        DDAContext ctx;

        ctx.step_sign = sign(dir);
        ctx.exit_value = ctx.step_sign * 0.5 + vec3(0.5);
        ctx.edge_distance = ctx.exit_value - (ctx.step_sign * fract(voxel_space_org));
        ctx.delta_t = 1.0 / (abs(dir) * voxel_count);
        ctx.max_t = abs(ctx.edge_distance) * ctx.delta_t;
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

        return ctx;
    }

    void StepDDAContext(inout DDAContext ctx)
    {
        ctx.mask.w =
            (1.0 - step(ctx.exit_value.x, ctx.voxel_coords.x * ctx.step_sign.x))
            * (1.0 - step(ctx.exit_value.y, ctx.voxel_coords.y * ctx.step_sign.y))
            * (1.0 - step(ctx.exit_value.z, ctx.voxel_coords.z * ctx.step_sign.z));

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

        ctx.voxel_coords += ctx.voxel_step * ctx.mask.xyz * ctx.mask.w;
        ctx.max_t += ctx.delta_t * ctx.mask.xyz * ctx.mask.w;
    }
    )";

    static char const region_fshader[] = R"(
    layout(location = 0) in vec3 iFS_volume_coords;
    layout(location = 1) in vec3 iFS_camera_position;
    layout(location = 0) out vec4 color;
    layout(set = 0, binding = 0) uniform usampler3D region;
    layout(set = 0, binding = 2) uniform sampler3D chunk;

    layout(set = 0, binding = 3) uniform sampler3D chunks[128];

    layout(std140, set = 0, binding = 1) uniform MatrixBlock {
        mat4 projection;
        mat4 view_model;
        mat4 view;
        ivec2 screen_size;
    };

    #define REGION_SIZE 32.0
    #define SECTION_SIZE 16.0

    void main() {
        vec2 uv = vec2(gl_FragCoord.x, gl_FragCoord.y) / vec2(screen_size.x, screen_size.y);
        vec3 dir =
            inverse(mat3(view_model)) *
            inverse(mat3(projection))
            * vec3(uv * 2.0 - vec2(1.0), 1.0);
        dir = normalize(dir);
        //dir = -dir;
        vec3 step_sign = sign(-dir);
        vec3 max_t = abs(((step_sign * 0.5 + vec3(0.5)) - (step_sign * iFS_volume_coords)) / dir);

        float depth = distance(iFS_camera_position * 0.5 + vec3(0.5), iFS_volume_coords);

        vec3 org = iFS_volume_coords
            - dir * 0.00001
            - (dir * min(depth, min(max_t.x, min(max_t.y, max_t.z))));

        mat4 world_mat = projection * view_model;

        DDAContext ctx = InitDDAContext(org, dir, REGION_SIZE);
        float d = 0.0;
        float t = 0.0;

        bool voxel_rendered = false;
        DDAContext ctx_cache;
        vec3 org_cache;

        uint hard_stop = 0;

        while (!voxel_rendered && ctx.mask.w == 1.0 && hard_stop < 16)
        {
        hard_stop++;

        bool chunk_rendered = false;
        uint chunk_index = 0xffff;
        while (d < 64.0 && ctx.mask.w == 1.0)
        {
            uvec2 data = texture(region, ctx.voxel_coords).xy;
            if (data.y != 0xffff)
            {
                ctx_cache = ctx;
                org_cache = org;

                ctx.max_t -= ctx.delta_t * ctx.mask.xyz * ctx.mask.w;
                float step_t = min(ctx.max_t.x, min(ctx.max_t.y, ctx.max_t.z));
                vec3 hit_pos = (org + dir*step_t) * REGION_SIZE * (1.0 - 0.00001*ctx.step_sign);
                org = hit_pos - floor(ctx.voxel_coords * REGION_SIZE);

                ctx = InitDDAContext(org, dir, SECTION_SIZE);
                chunk_rendered = true;
                chunk_index = data.y;
                break;
            }

            chunk_index = data.y;
            float step = (float(data.x) / 65535.0) * ctx.mask.w;
            StepDDAContext(ctx);

            vec3 max_t = (ctx.max_t - ctx.delta_t * ctx.mask.xyz) * ctx.mask.w;
            t = min(max_t.x, min(max_t.y, max_t.z));

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
        else if (voxel_rendered)
        {
            vec3 max_t = (ctx.max_t - ctx.delta_t * ctx.mask.xyz) * ctx.mask.w;
            //t += min(max_t.x, min(max_t.y, max_t.z)) / SECTION_SIZE;
            t = min(max_t.x, min(max_t.y, max_t.z));

            //ctx = ctx_cache;
            //ctx.max_t -= ctx.delta_t * ctx.mask.xyz * ctx.mask.w;
            //float root_t = min(ctx.max_t.x, min(ctx.max_t.y, ctx.max_t.z));
            //ctx.max_t -= ctx.delta_t * ctx.mask.xyz * ctx.mask.w;
            //float section_t = min(ctx.max_t.x, min(ctx.max_t.y, ctx.max_t.z));
            //t = distance((org_cache + dir*t/REGION_SIZE) * 2.0 - vec3(1.0), iFS_camera_position);
            //org = org_cache;

            vec3 hit_pos = (org_cache + dir*t);
            //t = distance(hit_pos * 2.0 - vec3(1.0), iFS_camera_position);
        }

        }

        if (!voxel_rendered)
        {
            color = vec4(vec3((d.x/16.0)), d.x/32.0);
        }
        else
        {
            color = vec4(org+dir*t,1.0);
            //color = vec4(vec3(t*2.0), 1.0);
            //color = vec4((org_cache + dir*t), 1.0);
        }
    }
    )";

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
                                            "#define CHUNK_VOXEL_COUNT 32.0",
                                            voxel_traversal_func,
                                            region_fshader
                                        });

        if (_state->region_pipeline != VK_NULL_HANDLE)
            vkDestroyPipeline(_vulkan->device, _state->region_pipeline, nullptr);

        _state->region_pipeline = _vulkan->CreatePipeline(
            _state->pipeline_layout,
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
}

bool Update(void* _pstate, vktk::Context* _vulkan,
            iotk::input_t const* _input,
            iotk::input_t const* _past_input)
{
    state_t* _state = (state_t*)_pstate;

    _state->time += _input->time_delta;

    if (_input->key_down[iotk::eKey::kEscape])
        return false;

    if (_input->button_down & iotk::fMouseButton::kLeftBtn)
    {
        numtk::vec2<int32_t> last_position{ _past_input->cursor[0], _past_input->cursor[1] };
        numtk::vec2<int32_t> position{ _input->cursor[0], _input->cursor[1] };
        numtk::vec2<int32_t> delta = position - last_position;
        _state->turntable += numtk::vec3f{ 0.01f * (float)delta.x(), 0.01f * (float)delta.y(), 0.f };
    }

    if (_input->button_down & iotk::fMouseButton::kRightBtn)
    {
        _state->y_offset += 0.01f * (float)(_input->cursor[1] -  _past_input->cursor[1]);
    }

    {
        //_state->turntable.z() += (float)_input->wheel_delta * 0.001f;
        _state->turntable.z() *= (1.f + 0.0005f * (float)_input->wheel_delta);
        if (_state->turntable.z() < 0.01f)
            _state->turntable.z() = 0.01f;
    }

    {
        numtk::vec3<int32_t> next_section = _state->current_section;
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

        if (next_section != _state->current_section)
        {
            LoadSection(_state, _vulkan, next_section);

            _vulkan->ScheduleRoutine(
                _state->last_submit,
                [chunk_view = _state->chunk_view](vktk::Context& _context) {
                    vkDestroyImageView(_context.device, chunk_view, nullptr);
                });

            _state->chunk_view = _vulkan->CreateImageView(_state->chunk_texture);
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

    VkDescriptorImageInfo region_image_info{};
    region_image_info.sampler = _state->sampler;
    region_image_info.imageView = _state->region_view;
    region_image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkDescriptorBufferInfo buffer_info = {};
    buffer_info.buffer = _state->matrix_buffer;
    buffer_info.offset = 0;
    buffer_info.range = VK_WHOLE_SIZE;

    VkDescriptorImageInfo chunk_image_info{};
    chunk_image_info.sampler = _state->sampler;
    chunk_image_info.imageView = _state->chunk_view;
    chunk_image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    {
        VkWriteDescriptorSet desc_writes[3];
        {
            VkWriteDescriptorSet& desc_write = desc_writes[0];
            desc_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            desc_write.pNext = nullptr;
            desc_write.dstSet = _state->descriptor_set;
            desc_write.dstBinding = 0u;
            desc_write.dstArrayElement = 0u;
            desc_write.descriptorCount = 1u;
            desc_write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            desc_write.pImageInfo = &region_image_info;
            desc_write.pBufferInfo = nullptr;
            desc_write.pTexelBufferView = nullptr;
        }

        {
            VkWriteDescriptorSet& desc_write = desc_writes[1];
            desc_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            desc_write.pNext = nullptr;
            desc_write.dstSet = _state->descriptor_set;
            desc_write.dstBinding = 1u;
            desc_write.dstArrayElement = 0u;
            desc_write.descriptorCount = 1u;
            desc_write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            desc_write.pImageInfo = nullptr;
            desc_write.pBufferInfo = &buffer_info;
            desc_write.pTexelBufferView = nullptr;
        }

        {
            VkWriteDescriptorSet& desc_write = desc_writes[2];
            desc_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            desc_write.pNext = nullptr;
            desc_write.dstSet = _state->descriptor_set;
            desc_write.dstBinding = 2u;
            desc_write.dstArrayElement = 0u;
            desc_write.descriptorCount = 1u;
            desc_write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            desc_write.pImageInfo = &chunk_image_info;
            desc_write.pBufferInfo = nullptr;
            desc_write.pTexelBufferView = nullptr;
        }
        vkUpdateDescriptorSets(_vulkan->device, 3u, desc_writes, 0u, nullptr);
    }

    {
        std::vector<VkDescriptorImageInfo> section_images_info(_state->section_index);
        for (uint32_t section_index = 0u; section_index < _state->section_index; ++section_index)
        {
            VkDescriptorImageInfo& image_info = section_images_info[section_index];
            image_info.sampler = _state->sampler;
            image_info.imageView = _state->section_views[section_index];
            image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }

        VkWriteDescriptorSet desc_write = {};
        desc_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        desc_write.pNext = nullptr;
        desc_write.dstSet = _state->descriptor_set;
        desc_write.dstBinding = 3u;
        desc_write.dstArrayElement = 0u;
        desc_write.descriptorCount = _state->section_index;
        desc_write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        desc_write.pImageInfo = section_images_info.data();
        desc_write.pBufferInfo = nullptr;
        desc_write.pTexelBufferView = nullptr;

        vkUpdateDescriptorSets(_vulkan->device, 1u, &desc_write, 0u, nullptr);
    }

    numtk::mat4f* matrix = (numtk::mat4f*)_state->staging_area;
    float how = (float)_swapchain.size.height / (float)_swapchain.size.width;
    numtk::vec3f forward = {
        std::cos(_state->turntable[1]) * std::cos(_state->turntable[0]),
        std::sin(_state->turntable[1]),
        std::cos(_state->turntable[1]) * std::sin(_state->turntable[0])
    };
    forward = forward.normalise();
    numtk::vec3f up = { 0.f, 1.f, 0.f };
    if (std::abs(1.f - forward.dot(numtk::vec3f{ 0.f, 1.f, 0.f })) > 1e-3f)
        up = forward.cross(numtk::vec3f{ 0.f, 1.f, 0.f }).cross(forward).normalise();
    else
        up = forward.cross(numtk::vec3f{ 0.f, 0.f, 1.f }).cross(forward).normalise();

#if 0
    std::cout << forward.norm() << std::endl;
    std::cout << _state->turntable.x() << " " << _state->turntable.y() << std::endl;
    std::cout << forward.x() << " " << forward.y() << " " << forward.z() << std::endl;
#endif

    numtk::vec3f center = (numtk::vec3f{
        (float)_state->current_section.x() + 0.5f,
        (float)_state->current_section.y() + 0.5f,
        (float)_state->current_section.z() + 0.5f
    } / 16.f) - numtk::vec3f{ 1.f, 1.f, 1.f } + numtk::vec3f{ 0.f, _state->y_offset, 0.f };

    matrix[0] =
        numtk::mat4f::Perspective_VulkanNDC(0.1f, 100.f, 90.f*numtk::kDegrees, how);
    matrix[2] =
        numtk::mat4f::View({ 0.f, 0.f, -1.f }, { 0.f, 1.f, 0.f }, { 0.f, 0.f, _state->turntable.z() });
    matrix[1] =
        matrix[2]
        * numtk::mat4f::View(forward, up, center);
    ((int32_t*)(matrix + 3))[0] = _swapchain.size.width;
    ((int32_t*)(matrix + 3))[1] = _swapchain.size.height;

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
                              sizeof(numtk::mat4f) * 3 + sizeof(numtk::vec2f));

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

    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _state->region_pipeline);
    vkCmdBindDescriptorSets(command_buffer,
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            _state->pipeline_layout, 0u,
                            1u, &_state->descriptor_set,
                            0u, nullptr);

    vkCmdDraw(command_buffer, 36u, 1u, 0u, 0u);

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
    vkDestroyPipelineLayout(_vulkan->device, _state->pipeline_layout, nullptr);
    _state->pipeline_layout = VK_NULL_HANDLE;
    vkDestroySampler(_vulkan->device, _state->sampler, nullptr);
    _state->sampler = VK_NULL_HANDLE;

    vkFreeDescriptorSets(_vulkan->device, _state->descriptor_pool, 1, &_state->descriptor_set);
    _state->descriptor_set = VK_NULL_HANDLE;
    vkDestroyDescriptorPool(_vulkan->device, _state->descriptor_pool, nullptr);
    _state->descriptor_pool = VK_NULL_HANDLE;
    vkDestroyDescriptorSetLayout(_vulkan->device, _state->descset_layout, nullptr);
    _state->descset_layout = VK_NULL_HANDLE;

    vkDestroyImage(_vulkan->device, _state->chunk_texture.image, nullptr);
    _state->chunk_texture.image = VK_NULL_HANDLE;
    vkFreeMemory(_vulkan->device, _state->chunk_texture.memory, nullptr);
    _state->chunk_texture.memory = VK_NULL_HANDLE;
    vkDestroyImageView(_vulkan->device, _state->chunk_view, nullptr);
    _state->chunk_view = VK_NULL_HANDLE;

    vkDestroyImage(_vulkan->device, _state->region_texture.image, nullptr);
    _state->region_texture.image = VK_NULL_HANDLE;
    vkFreeMemory(_vulkan->device, _state->region_texture.memory, nullptr);
    _state->region_texture.memory = VK_NULL_HANDLE;
    vkDestroyImageView(_vulkan->device, _state->region_view, nullptr);
    _state->region_view = VK_NULL_HANDLE;

    vkUnmapMemory(_vulkan->device, _state->staging_buffer.alloc.memory);

    _vulkan->DestroyBuffer(_state->matrix_buffer);
    _vulkan->DestroyBuffer(_state->staging_buffer);

    if (_state->last_submit != VK_NULL_HANDLE)
    {
        _vulkan->ScheduleFenceDestroy(_state->last_submit);
        _state->last_submit = VK_NULL_HANDLE;
    }
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
