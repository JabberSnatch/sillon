#include "_voxels.hh"

#define ZIPTK_IMPLEMENTATION
#define MCTK_IMPLEMENTATION
#include "mctk/mctk.hh"

#include <numtk.hh>
#include <dstk.hh>

#include "guitk.hh"
#include "systk.hh"
#include "vktk.hh"

#define VOXTK_IMPLEMENTATION
#include <voxtk.hh>
#define VOXTK_IMPORT_IMPLEMENTATION
#include "voxtk-importers/voxtk_import.hh"

#include <bit>
#include <memory>
#include <unordered_map>

//#define TRAVERSAL_V1
#define TRAVERSAL_V2

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
#if defined(TRAVERSAL_V1)
    numtk::vec2f forward_spherical = { .5f, -0.25f };
    numtk::vec3f position = { 16.f, -0.3f, 16.f };
#elif defined(TRAVERSAL_V2)
    numtk::vec2f forward_spherical = { 0.f, 0.f };
    numtk::vec3f position = { 0.f, 512.f, 0.f };
#endif

    float velocity = 5.f;

    void Update(iotk::input_t const* _input,
                iotk::input_t const* _past_input);
    numtk::mat4f Matrix() const;
    bool updated;
};

struct VoxelRegionPackage
{
    uint32_t area_size;
    uint32_t node_size;
    float area_scale;
    int32_t stack_depth;
    int32_t display_depth;
    uint32_t leaves_begin;
    std::vector<vktk::Texture> masks{};
    std::vector<vktk::Texture> offsets{};
    std::vector<vktk::Texture> payloads{};
};

VoxelRegionPackage UploadVoxelRegion(vktk::Context* vulkan,
                                     voxtk::BinaryRegion const& region,
                                     uint32_t area_size,
                                     uint32_t node_size);

struct dispatch_runtime_t
{
    vkrg::runtime::DispatchContext Context() {
        return vkrg::runtime::DispatchContext{
            *registry,
            *vulkan,
            graph
        };
    }

    vkrg::layout::DispatchGraph graph;
    vktk::Context* vulkan;
    dstk::Registry* registry;
    struct voxel_state_t const* state;
    void* payload;

    void (*DestroyPayload)(dispatch_runtime_t&);

    void (*ConstantResources)(dispatch_runtime_t&);
    void (*RenderResources)(dispatch_runtime_t&, vktk::Swapchain const&);
    void (*Update)(dispatch_runtime_t&);
    void (*Dispatch)(dispatch_runtime_t&, vktk::CommandBuffer&, vktk::Swapchain const&);
    void (*PostSubmit)(dispatch_runtime_t&, VkFence);
};

void SparseRegionUpdateBindings(dispatch_runtime_t& _rt);

struct voxel_state_t
{
    guitk::Context gui_context;
    guitk::WidgetStates const* current_gui_record;

    float time = 0.f;

    guitk::Frame framerate_frame;
    float framerate_average = 0.f;
    std::string framerate_label;
    bool enable_indirect_light = false;

    //TurntableCamera camera;
    FlightCamera camera;

    numtk::vec2f sun_spherical = { -2.f, 0.25f };

    uint32_t frame_index = 0;
    bool pixel_cycling_enabled = false;

    uint32_t progressive_frame_index = 0;

    dstk::Registry core_registry;
    dispatch_runtime_t forward_runtime;
    dispatch_runtime_t pixel_cycling_runtime;
    dispatch_runtime_t* current_runtime;
    dispatch_runtime_t sparse_runtime;
    dispatch_runtime_t progressive_runtime;

    VkFence last_submit;

    vktk::Texture region_texture;
    RegionData region;

    std::vector<uint16_t> section_map{};
    uint32_t section_index = 0;
    std::vector<vktk::Texture> section_textures{};

    numtk::vec3i stencil_location{ 0, 0, 0 };
    voxtk::BinaryRegion initial_region;
    voxtk::BinaryRegion voxel_region{ { 512u, 512u, 512u } };
    VoxelRegionPackage region_package;
};

struct MatrixBlock
{
    numtk::mat4f projection;
    numtk::mat4f view_model;
    numtk::mat4f view;
    numtk::vec2f screen_offset;
    numtk::vec2i screen_size;
    float padding[12];
};

struct SparseRegionPushConstants
{
    numtk::vec3f light_dir;
    float display_mode;
    float node_size;
    int32_t stack_depth;
    uint32_t sample_offset;
    uint32_t leaves_begin;
};

struct SkyboxPushConstants
{
    numtk::mat4f projection;
    numtk::mat4f view;
    numtk::vec4f position;
    numtk::vec3f light_dir;
};

dispatch_runtime_t DenseRenderingPipeline(
    voxel_state_t* _state, vktk::Context* _vulkan);
dispatch_runtime_t DensePixelCyclingRenderingPipeline(
    voxel_state_t* _state, vktk::Context* _vulkan);
dispatch_runtime_t SparseRenderingPipeline(
    voxel_state_t* _state, vktk::Context* _vulkan);
dispatch_runtime_t ProgressiveRenderingPipeline(
    voxel_state_t* _state, vktk::Context* _vulkan);

void BindGUIRuntime(voxel_state_t* _state, vktk::Context* _vulkan,
                    dstk::Registry::Node _subpass_node);
void BindSparseRegionRuntime(voxel_state_t* _state, vktk::Context* _vulkan,
                             dstk::Registry::Node _subpass_node);

vkrg::layout::DispatchStage RegionPassDispatchStageInfo();
vkrg::layout::DispatchStage SparseRegionDispatchStageInfo();
vkrg::layout::DispatchStage SkyboxDispatchStageInfo();
vkrg::layout::DispatchStage ResolveDispatchStageInfo();
vkrg::layout::DispatchStage GUIFramesDispatchStageInfo();
vkrg::layout::DispatchStage GUIGlyphsDispatchStageInfo();

vkrg::layout::DispatchGraph SparseRegionRenderSystem(dstk::Registry& _registry);
vkrg::layout::DispatchGraph FullFrameRenderGraph(dstk::Registry& _registry);
vkrg::layout::DispatchGraph PixelCyclingRenderGraph(dstk::Registry& _registry);

vkrg::layout::DispatchGraph SparseRegionRenderGraph(dstk::Registry& _registry);
vkrg::layout::DispatchGraph GUIRenderGraph(dstk::Registry& _registry);

std::unordered_map<std::string, numtk::vec3<uint8_t>> const& BlockColorMap();

void DecodeChunk(voxtk::BinaryRegion& region,
                 mctk::MCAChunkData const& chunk_data,
                 numtk::vec2u const& chunk_xz)
{
    using namespace mctk;

    std::vector<NBTField> chunk_fields = MCAPackedNBTDecode(chunk_data);
    if (chunk_fields.empty())
        return;

    std::unordered_map<std::string, numtk::vec3<uint8_t>> const& block_color = BlockColorMap();
    NBTCompound const& root = *(NBTCompound const*)chunk_fields[0].payload;
    for (NBTField const& field0 : root)
    {
        if (field0.name == "sections"
            && field0.type == TAG_List)
        {
            NBTList const& sections = *(NBTList const*)field0.payload;
            NBTCompound const* compounds = (NBTCompound const*)sections.data;
            for (uint32_t index = 0u; index < sections.size; ++index)
            {
                NBTCompound const& compound = compounds[index];
                //int32_t section_index = -1;
                //RegionData::SectionData section_data = {};
                NBTCompound const* block_states = nullptr;
                int8_t section_y = -1;

                for (NBTField const& field1 : compound)
                {
                    if (field1.name == "Y"
                        && field1.type == TAG_Byte)
                        section_y = (*(int8_t const*)field1.payload) + 4;

                    if (field1.name == "block_states"
                        && field1.type == TAG_Compound)
                    {
                        block_states = (NBTCompound const*)field1.payload;
                    }
                }

                if (section_y >= 0 && block_states)
                {
                    numtk::vec3u section_begin =
                        numtk::vec3u{ chunk_xz.x(), (uint32_t)section_y, chunk_xz.y() } * 16u;
                    numtk::vec3u section_end =
                        section_begin + numtk::vec3u::Constant(16u);

                    //NBTCompound const& block_states = *(NBTCompound const*)field1.payload;
                    NBTLongArray const* block_data_field = nullptr;
                    uint32_t palette_size = 0u;
                    std::vector<std::string> palette_data = {};

                    for (NBTField const& field2 : *block_states)
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
                        //region.Set(section_begin, section_end, (uint8_t)0);
                        region.Set(numtk::bounds3u{ section_begin, section_end-section_begin }, true);
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

                        numtk::vec3u point = section_begin + numtk::vec3u{ x, y, z };
#if 0
                        if (palette_data[block_id] != "air"
                            && palette_data[block_id] != "glow_lichen")
                        {
                            numtk::vec3<uint8_t> color = { 255, 255, 255 };
                            if (block_color.count(palette_data[block_id]))
                                color = block_color.at(palette_data[block_id]);
                            region.StoreData(point, 0, color);
                        }
#endif
                        if (block_color.count(palette_data[block_id]))
                            region.StoreData(point, 0, block_color.at(palette_data[block_id]));

                        data_offset += bit_count;
                        ++block_index;
                    }
                }
            }
        }
    }
}

voxtk::BinaryRegion DeserialiseRegion(std::string const& file_path)
{
    systk::Timer scope_time{ "DeserialiseRegion",
        [](std::string const& tag, float time) {
            std::cout << "[" << tag << "] " << time << " s" << std::endl;
        }
    };

    std::vector<uint8_t> region_file = systk::LoadFile(file_path.c_str());
    mctk::MCAFileData mca_data = mctk::MCADecode(region_file.data(), region_file.size());
    voxtk::BinaryRegion output{ { 512u, 512u, 512u } };
    output.DeclareLayer(0, numtk::vec3<uint8_t>{ 0, 0, 0 });

    for (uint32_t index = 0; index < 1024; ++index)
    {
        uint32_t chunk_x = index % 32;
        uint32_t chunk_z = index / 32;
        DecodeChunk(output, mca_data.chunks[index], numtk::vec2u{ chunk_x, chunk_z });
    }

    return output;
}

void LoadSection(voxel_state_t* state, vktk::Context* vulkan, vktk::CommandBuffer& update_commands,
                 numtk::vec3<int32_t> const& coords)
{
    std::vector<std::unique_ptr<uint8_t[]>> mip_levels = {};

    voxtk::BinaryRegion const& region = state->voxel_region;
    uint16_t section_count = state->section_map[coords[0] + coords[1]*32 + coords[2]*32*32];
    if (section_count != 0u && section_count != 4096u)
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
            for (uint32_t pointz = 0; pointz < 16; ++pointz)
            {
                for (uint32_t pointy = 0; pointy < 16; ++pointy)
                {
                    for (uint32_t pointx = 0; pointx < 16; ++pointx)
                    {
                        uint32_t index = pointx + pointy*16u + pointz*256u;
                        numtk::vec3u point {
                            (uint32_t)coords.x * 16u + pointx,
                            (uint32_t)coords.y * 16u + pointy,
                            (uint32_t)coords.z * 16u + pointz
                        };
                        blocks[index] = (region.Test(point) ? 255u : 0u);
                    }
                }
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
                section_info[1] = (uint16_t)section_index;
            });

        state->section_index++;
    }
    else
    {
        update_commands.UpdateTexture(
            state->region_texture,
            VkOffset3D{ coords[0], coords[1], coords[2] },
            VkExtent3D{ 1, 1, 1 },
            [section_count](void* mapped_data) {
                uint16_t* section_info = (uint16_t*)mapped_data;
                if (section_count == 0)
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

void LoadArea(voxel_state_t* _state, vktk::Context* _vulkan,
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
    for (int32_t x = _begin.x; x < _end.x; ++x)
    {
        for (int32_t y = _begin.y; y < _end.y; ++y)
        {
            for (int32_t z = _begin.z; z < _end.z; ++z)
            {
                LoadSection(_state, _vulkan, update_commands, { x, y, z });
            }
        }
    }

    update_commands.End();
    update_commands.Submit();
}

void LoadRegion(voxel_state_t* state, vktk::Context* vulkan)
{
    systk::Timer scope_time{ "LoadRegion",
        [](std::string const& tag, float time) {
            std::cout << "[" << tag << "] " << time << " s" << std::endl;
        }
    };

    voxtk::BinaryRegion const& region = state->voxel_region;

    state->section_map.resize(32*32*32);

    for (uint32_t zindex = 0; zindex < 32; ++zindex)
    {
        for (uint32_t yindex = 0; yindex < 32; ++yindex)
        {
            for (uint32_t xindex = 0; xindex < 32; ++xindex)
            {
                uint32_t index = xindex + yindex*32 + zindex*32*32;
                state->section_map[index] = 0;
                if (yindex < 24)
                {
                    uint16_t count = 0;

                    for (uint32_t pointz = 0; pointz < 16; ++pointz)
                    {
                        for (uint32_t pointy = 0; pointy < 16; ++pointy)
                        {
                            for (uint32_t pointx = 0; pointx < 16; ++pointx)
                            {
                                numtk::vec3u point{
                                    xindex * 16u + pointx,
                                    yindex * 16u + pointy,
                                    zindex * 16u + pointz
                                };
                                if (region.Test(point))
                                    ++count;
                            }
                        }
                    }

                    state->section_map[index] = count;
                }
            }
        }
    }

    state->region_texture = vulkan->CreateTexture(
        32, 32, 32,
        VK_IMAGE_TYPE_3D, VK_FORMAT_R16G16_UINT,
        VK_IMAGE_USAGE_SAMPLED_BIT,
        [state](void* mapped_data) {
            uint16_t* sections = (uint16_t*)mapped_data;

            for (uint32_t zindex = 0; zindex < 32; ++zindex)
            {
                for (uint32_t yindex = 0; yindex < 32; ++yindex)
                {
                    for (uint32_t xindex = 0; xindex < 32; ++xindex)
                    {
                        uint32_t index = xindex + yindex*32 + zindex*32*32;
                        sections[index * 2+1] = 0xffff;
                        sections[index * 2] = (uint16_t)(((float)state->section_map[index] / 4096.f) * 65535.f);
                    }
                }
            }
        });
}

VoxelRegionPackage UploadVoxelRegion(vktk::Context* vulkan,
                                     voxtk::BinaryRegion const& region,
                                     uint32_t area_size,
                                     uint32_t node_size)
{
    VoxelRegionPackage package = {};
    package.area_size = area_size;
    package.node_size = node_size;

    package.stack_depth = 0;
    for (uint32_t region_size = package.area_size*4;
         region_size > 1;
         region_size /= package.node_size*4)
        package.stack_depth += 2;
    package.display_depth = package.stack_depth;

    uint32_t kScaleRatio = area_size / node_size;
    uint32_t kLogScale = (uint32_t)std::popcount((node_size*4) - 1u);
    uint32_t kNodeMortonMask = (node_size - 1u);
    uint32_t kNodeMortonShift = (uint32_t)std::popcount(kNodeMortonMask);
    uint32_t kChunkMortonMask = 0x3;
    uint32_t kChunkMortonShift = 2;
    uint32_t kLevelMortonShift = (kNodeMortonShift+kChunkMortonShift)*3;

    std::vector<voxtk::BinaryRegion> lods = {};

    voxtk::BinaryRegion const* src = &region;
    for (uint32_t lod_size = kScaleRatio, src_index = 0u;
         lod_size > 1;
         (lod_size /= node_size*4, ++src_index))
    {
        lods.push_back(voxtk::BinaryRegion{ numtk::vec3u::Constant(lod_size) });

        if (src_index)
            src = &lods[src_index-1];
        voxtk::BinaryRegion& dst = lods.back();

        for (uint32_t dstz = 0; dstz < dst.size.z; ++dstz)
        {
            for (uint32_t dsty = 0; dsty < dst.size.y; ++dsty)
            {
                for (uint32_t dstx = 0; dstx < dst.size.x; ++dstx)
                {
                    numtk::vec3u dstbase{ dstx, dsty, dstz };

                    bool bit = []
                        (uint32_t node_size,
                         voxtk::BinaryRegion const& src,
                         numtk::vec3u const& dstbase) {
                        for (uint32_t srcz = 0; srcz < node_size; ++srcz)
                        {
                            for (uint32_t srcy = 0; srcy < node_size; ++srcy)
                            {
                                for (uint32_t srcx = 0; srcx < node_size; ++srcx)
                                {
                                    numtk::vec3u srcbase = dstbase*node_size*4
                                        + numtk::vec3u{ srcx, srcy, srcz }*4;
                                    uint64_t srcbits = src.GetEnclosingKernel(srcbase);
                                    if (srcbits != 0)
                                        return true;
                                }
                            }
                        }
                        return false;
                    }(node_size, *src, dstbase);

                    dst.Set(dstbase, bit);
                }
            }
        }
    }

    lods.push_back(voxtk::BinaryRegion{ numtk::vec3u::Constant(1) });
    lods.back().Set(numtk::vec3u::Constant(0), true);

    uint32_t current_offset = 1u;
    uint32_t group_size = 1u;

    uint32_t morton_mask = kNodeMortonMask;

    uint32_t node_mask = 0;
    uint32_t chunk_mask = 0;
    uint32_t shift_depth = 0;

    uint32_t lod_count = lods.size()-1;
    for (uint32_t level_index = 0u; level_index < lod_count;
         ++level_index
             , morton_mask = (morton_mask << kNodeMortonShift*3) | kNodeMortonMask
             , node_mask = (kNodeMortonMask << (3*kChunkMortonShift)) | (node_mask << kLevelMortonShift)
             , chunk_mask = kChunkMortonMask | (chunk_mask << kLevelMortonShift)
             , shift_depth += kLevelMortonShift)
    {
        uint32_t lod_index = lod_count-1 - level_index;
        voxtk::BinaryRegion const& lod = lods[lod_count-1 - level_index];
        numtk::vec3u nextlod_size = numtk::max(lod.size / (node_size * 4),
                                               numtk::vec3u::Constant(1));
        numtk::vec3u nextlod_chunkcount = numtk::max(nextlod_size / 4,
                                                    numtk::vec3u::Constant(1));
        numtk::vec3u nextlod_chunksize = (level_index == 0)
            ? numtk::vec3u{ 4, 4, 4 }
            : numtk::min(lods[lod_count-level_index].size,
                         numtk::vec3u{ 4, 4, 4 });

        uint32_t nextlod_count = 1u << shift_depth;

        for (uint32_t index = 0u; index < nextlod_count; ++index)
        {
            std::vector<uint32_t> level_offsets{};

            numtk::vec3u const nextlod_base{
                numtk::BitExtract(index, node_mask | chunk_mask),
                numtk::BitExtract(index, (node_mask << kNodeMortonShift) | (chunk_mask << kChunkMortonShift)),
                numtk::BitExtract(index, (node_mask << (2*kNodeMortonShift)) | (chunk_mask << (2*kChunkMortonShift)))
            };

            if (level_index != 0u && !lods[lod_count - level_index].Test(nextlod_base))
                continue;

            numtk::vec3u const lod_base = nextlod_base * node_size*4;

            numtk::vec3u const valid_range{
                std::min(node_size, std::max((lod.size.x-lod_base.x)/4u, 1u)),
                std::min(node_size, std::max((lod.size.y-lod_base.y)/4u, 1u)),
                std::min(node_size, std::max((lod.size.z-lod_base.z)/4u, 1u))
            };

            package.masks.push_back(
                vulkan->CreateTexture(
                    node_size, node_size, node_size,
                    VK_IMAGE_TYPE_3D, VK_FORMAT_R32G32_UINT,
                    VK_IMAGE_USAGE_SAMPLED_BIT,
                    [&lod, &lod_base, &level_offsets, &valid_range, &current_offset, lod_index, node_size](void* mapped_data) {
                        uint64_t* output = (uint64_t*)mapped_data;
                        for (uint32_t zindex = 0; zindex < valid_range.z; ++zindex)
                        {
                            for (uint32_t yindex = 0; yindex < valid_range.y; ++yindex)
                            {
                                for (uint32_t xindex = 0; xindex < valid_range.x; ++xindex)
                                {
                                    numtk::vec3u base = numtk::vec3u{
                                        xindex * 4,
                                        yindex * 4,
                                        zindex * 4
                                    } + lod_base;

                                    uint64_t bits = lod.GetEnclosingKernel(base);
                                    output[xindex + yindex*node_size + zindex*node_size*node_size] = bits;

                                    level_offsets.push_back(current_offset);
                                    current_offset += std::popcount(bits);
                                }

                                if (valid_range.x != node_size)
                                    level_offsets.resize(level_offsets.size()
                                                         + (node_size-valid_range.x),
                                                         level_offsets.back());
                            }

                            if (valid_range.y != node_size)
                                level_offsets.resize(level_offsets.size()
                                                     + (node_size-valid_range.y)*node_size,
                                                     level_offsets.back());
                        }

                        if (valid_range.z != node_size)
                            level_offsets.resize(level_offsets.size()
                                                 + (node_size-valid_range.z)*node_size*node_size,
                                                 level_offsets.back());
                    }
                )
            );

            package.offsets.push_back(
                vulkan->CreateTexture(
                    node_size, node_size, node_size,
                    VK_IMAGE_TYPE_3D, VK_FORMAT_R32_UINT,
                    VK_IMAGE_USAGE_SAMPLED_BIT,
                    [&level_offsets, &valid_range, node_size](void* mapped_data) {
                        uint32_t* output = (uint32_t*)mapped_data;
                        std::memcpy(output, level_offsets.data(), node_size*node_size*node_size*sizeof(uint32_t));
                    }
                )
            );
        }
    }

    uint32_t max_fence_count = 0u;

    vktk::CommandBuffer upload_commands = vulkan->CreateCommandBuffer(vulkan->present_queue);
    upload_commands.Begin();

    package.leaves_begin = package.masks.size();
    uint32_t level1_count = 1 << shift_depth;
    for (uint32_t index = 0u; index < level1_count; ++index)
    {
        numtk::vec3u const lod1_base{
            numtk::BitExtract(index, node_mask | chunk_mask),
            numtk::BitExtract(index, (node_mask << kNodeMortonShift) | (chunk_mask << kChunkMortonShift)),
            numtk::BitExtract(index, (node_mask << (2*kNodeMortonShift)) | (chunk_mask << (2*kChunkMortonShift)))
        };

        if (!lods[0].Test(lod1_base))
            continue;

        numtk::vec3u const lod0_base = lod1_base * node_size*4u;

        vktk::Texture payload = vulkan->CreateTexture(
            node_size * 4u, node_size * 4u, node_size * 4u,
            VK_IMAGE_TYPE_3D, VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);

        vktk::Texture mask = vulkan->CreateTexture(
            node_size, node_size, node_size,
            VK_IMAGE_TYPE_3D, VK_FORMAT_R32G32_UINT,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);

        upload_commands.ImageTransitions(
            VK_PIPELINE_STAGE_HOST_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            {
                {
                    mask.image,
                    VK_ACCESS_NONE, VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_IMAGE_ASPECT_COLOR_BIT,
                },
                {
                    payload.image,
                    VK_ACCESS_NONE, VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_IMAGE_ASPECT_COLOR_BIT
                }
            });

        upload_commands.UpdateTexture(
            mask,
            VkOffset3D{ 0, 0, 0 },
            VkExtent3D{ node_size, node_size, node_size },
            [&region, &lod0_base, node_size](void* mapped_data) {
                uint64_t* output = (uint64_t*)mapped_data;
                for (uint32_t zindex = 0; zindex < node_size; ++zindex)
                {
                    for (uint32_t yindex = 0; yindex < node_size; ++yindex)
                    {
                        for (uint32_t xindex = 0; xindex < node_size; ++xindex)
                        {
                            numtk::vec3u base = numtk::vec3u{
                                xindex * 4,
                                yindex * 4,
                                zindex * 4
                            } + lod0_base;

                            uint64_t bits = region.GetEnclosingKernel(base);
                            output[xindex + yindex*node_size + zindex*node_size*node_size] = bits;
                        }
                    }
                }
            }
        );

        upload_commands.UpdateTexture(
            payload,
            VkOffset3D{ 0, 0, 0 },
            VkExtent3D{ node_size * 4, node_size * 4, node_size * 4 },
            [&region, &lod0_base, node_size](void* mapped_data) {
                uint32_t extent = node_size * 4;
                numtk::vec4<uint8_t>* output = (numtk::vec4<uint8_t>*)mapped_data;
                for (uint32_t zindex = 0; zindex < extent; ++zindex)
                {
                    for (uint32_t yindex = 0; yindex < extent; ++yindex)
                    {
                        for (uint32_t xindex = 0; xindex < extent; ++xindex)
                        {
                            numtk::vec3<uint8_t> const& data =
                                region.LoadData<numtk::vec3<uint8_t>>(
                                    lod0_base + numtk::vec3u{ xindex, yindex, zindex }, 0);

                            output[xindex
                                   + yindex*extent
                                   + zindex*extent*extent] = numtk::vec4<uint8_t>{
                                data.x, data.y, data.z, 0
#if 0
                                (uint8_t)255u,
                                ((xindex / 16) & (yindex / 16) & (zindex / 16)) == 0
                                ? (uint8_t)255u
                                : (uint8_t)0,
                                ((xindex / 16) & (yindex / 16) & (zindex / 16)) == 0
                                ? (uint8_t)255u
                                : (uint8_t)0,
                                0
#endif

#if 0
                                (uint8_t)((xindex * 256) / extent),
                                (uint8_t)((yindex * 256) / extent),
                                (uint8_t)((zindex * 256) / extent),
                                0
#endif
                            };
                        }
                    }
                }
            }
        );

        upload_commands.ImageTransitions(
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            {
                {
                    mask.image,
                    VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_IMAGE_ASPECT_COLOR_BIT
                },
                {
                    payload.image,
                    VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_IMAGE_ASPECT_COLOR_BIT
                }
            });

        package.masks.push_back(mask);
        package.payloads.push_back(payload);

        max_fence_count = std::max(max_fence_count, vulkan->fence_count);
    }

    upload_commands.End();
    upload_commands.Submit();

    return package;
}

void ConstantResources(void* _pstate, vktk::Context* _vulkan)
{
    voxel_state_t* _state = (voxel_state_t*)_pstate;
    _state->gui_context = guitk::CreateContext();

#if 0
    {
        numtk::vec3i offset_axis{ 1, 0, 1 };

        voxtk::VoxelMask testmask0 = voxtk::VoxelMask::FillArea({ 0, 0, 0 }, { 4, 4, 4 });
        testmask0 = testmask0.Shift(offset_axis * 1);

        voxtk::VoxelMask testmask1 = voxtk::VoxelMask::FillArea({ 0, 0, 0 }, { 4, 4, 4 });
        testmask1 = testmask1.Shift(offset_axis * 2);

        voxtk::VoxelMask testmask2 = voxtk::VoxelMask::FillArea({ 0, 0, 0 }, { 4, 4, 4 });
        testmask2 = testmask2.Shift(offset_axis * 3);

        voxtk::VoxelMask testmask3 = voxtk::VoxelMask::FillArea({ 0, 0, 0 }, { 4, 4, 4 });
        testmask3 = testmask3.Shift(offset_axis * 4);

        voxtk::VoxelMask testmask4 = voxtk::VoxelMask::FillArea({ 0, 0, 0 }, { 4, 4, 4 });
        testmask4 = testmask4.Shift(offset_axis * 5);

        voxtk::VoxelMask testmask5 = voxtk::VoxelMask::FillArea({ 0, 0, 0 }, { 4, 4, 4 });
        testmask5 = testmask5.Shift(offset_axis * 6);

        voxtk::VoxelMask testmask6 = voxtk::VoxelMask::FillArea({ 0, 0, 0 }, { 4, 4, 4 });
        testmask6 = testmask6.Shift(offset_axis * 7);

        voxtk::VoxelMask testmask7 = voxtk::VoxelMask::FillArea({ 0, 0, 0 }, { 4, 4, 4 });
        testmask7 = testmask7.Shift(offset_axis * 8);

        int i = 0;
    }
#endif

    uint32_t area_size = 0u;
    uint32_t node_size = 0u;
#if 1
    {
        voxtk::BinaryRegion src{ numtk::vec3u{ 16, 16, 16 } };
        for (uint32_t z = 0; z < 16; ++z)
            for (uint32_t y = 0; y < 16; ++y)
                for (uint32_t x = 0; x < 16; ++x)
                    src.Set(numtk::vec3u{ x, y, z }, !!(((x&1) + (y&1) + (z&1)) & 1));

        voxtk::BinaryRegion mask{ numtk::vec3u{ 2, 2, 2 } };
        mask.Set(numtk::bounds3u{{ 0, 0, 0 }, { 2, 2, 2 }}, true);

        voxtk::BinaryRegion src_shifted = src.Shift(numtk::vec3i{ 0, -7, 0 });

        _state->voxel_region = src.BitwiseAnd(numtk::vec3i{ 0, 7, 0 }, mask);
        //_state->voxel_region = src_shifted;
        //_state->voxel_region = src;

        area_size = 4u;
        node_size = 4u;
        int i = 0;
    }
#endif

    _state->forward_runtime = DenseRenderingPipeline(_state, _vulkan);
    _state->pixel_cycling_runtime = DensePixelCyclingRenderingPipeline(_state, _vulkan);
    _state->sparse_runtime = SparseRenderingPipeline(_state, _vulkan);

    _state->progressive_runtime = ProgressiveRenderingPipeline(_state, _vulkan);

#if defined(TRAVERSAL_V1)
    _state->voxel_region = DeserialiseRegion("../r.-1.6.mca");

#elif defined(TRAVERSAL_V2)

#if 1
#if 1
    _state->voxel_region = DeserialiseRegion("../r.-1.6.mca");
    area_size = 128u;
    node_size = 8u;

#else
    area_size = 1024u;
    node_size = 32u;

    {
        systk::Timer scope_time{ "COPC import",
            [](std::string const& tag, float time) {
                std::cout << "[" << tag << "] " << time << " s" << std::endl;
            }
        };

        _state->voxel_region = voxtk::importer::ImportCOPC("../LHD_FXX_0697_6520_PTS_C_LAMB93_IGN69.copc.laz", area_size * 4);
        //_state->voxel_region = voxtk::importer::ImportCOPC("../LHD_FXX_0704_6519_PTS_C_LAMB93_IGN69.copc.laz", 4096);
        //_state->voxel_region = voxtk::importer::ImportCOPC("../LHD_FXX_0697_6520_PTS_C_LAMB93_IGN69.copc.laz", 512);
    }
#endif

#endif

    _state->initial_region = _state->voxel_region;
    _state->region_package = UploadVoxelRegion(_vulkan, _state->voxel_region, area_size, node_size);

#endif

#ifdef TRAVERSAL_V1
    LoadRegion(_state, _vulkan);
    LoadArea(_state, _vulkan, { 0, 0, 0 }, { 32, 16, 32 });
#endif

#if 0
    _state->forward_runtime.ConstantResources(_state->forward_runtime);
    _state->pixel_cycling_runtime.ConstantResources(_state->pixel_cycling_runtime);
    _state->sparse_runtime.ConstantResources(_state->sparse_runtime);
#endif

    _state->progressive_runtime.ConstantResources(_state->progressive_runtime);

    return;
}

void RenderResources(void* _pstate,
                     vktk::Context* _vulkan,
                     vktk::Swapchain const& _swapchain)
{
    voxel_state_t* _state = (voxel_state_t*)_pstate;

#if 0
    _state->forward_runtime.RenderResources(_state->forward_runtime, _swapchain);
    _state->pixel_cycling_runtime.RenderResources(_state->pixel_cycling_runtime, _swapchain);
    _state->sparse_runtime.RenderResources(_state->sparse_runtime, _swapchain);
#endif

    _state->progressive_runtime.RenderResources(_state->progressive_runtime, _swapchain);
}

bool Update(void* _pstate, vktk::Context* _vulkan,
            iotk::input_t const* _input,
            iotk::input_t const* _past_input)
{
    voxel_state_t* _state = (voxel_state_t*)_pstate;

    bool old_indirect_light_state = _state->enable_indirect_light;
    bool reset_progressive_counter = false;
    {
        if (!(_state->frame_index % 10))
        {
            _state->framerate_label = std::to_string(_state->framerate_average);
            _state->framerate_average = 0.f;
        }
        _state->framerate_average += _input->time_delta / 10.f;

        guitk::BeginRecord(_state->gui_context, _input, _past_input);
        guitk::BeginFrame(_state->gui_context, _state->framerate_frame);
        guitk::Label(_state->gui_context, _state->framerate_label.c_str());

#if defined(TRAVERSAL_V1)
        guitk::Checkbox(_state->gui_context, &_state->pixel_cycling_enabled, "Pixel Cycling");
#elif defined(TRAVERSAL_V2)
        guitk::Checkbox(_state->gui_context, &_state->enable_indirect_light, "Indirect Lighting");
#endif

#if 0
        for (uint32_t i = 0; i < 4; ++i)
        {
            static bool storage[4];
            guitk::Checkbox(_state->gui_context, &storage[i], std::string("checkbox") + std::to_string(i));
        }
#endif
        guitk::EndFrame(_state->gui_context);

#if 0
        static std::vector<guitk::Frame> frames(4);
        for (uint32_t i = 0; i < 4; ++i)
        {
            guitk::BeginFrame(_state->gui_context, frames[i]);
            guitk::Label(_state->gui_context, "the quick brown fox jumps over the lazy dog");
            guitk::Label(_state->gui_context, "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG");
            guitk::Label(_state->gui_context, "{} [] :; \\| \"' `~ =+ -_ <> ,. ?/");
            guitk::Label(_state->gui_context, "12345 67890  !@#$% ^&*()");
            guitk::EndFrame(_state->gui_context);
        }
#endif
        guitk::WidgetStates const* gui_record = guitk::EndRecord(_state->gui_context);
        _state->current_gui_record = gui_record;

#if 0
        _state->sparse_runtime.Update(_state->sparse_runtime);
        _state->forward_runtime.Update(_state->forward_runtime);
        _state->pixel_cycling_runtime.Update(_state->pixel_cycling_runtime);
#endif
        _state->progressive_runtime.Update(_state->progressive_runtime);
    }

    _state->time += _input->time_delta;

    if (_input->key_down[iotk::eKey::kEscape])
        return false;

    if (!_state->current_gui_record->mouse_over &&
        _input->button_down & iotk::fMouseButton::kRightBtn)
    {
        numtk::vec2<int32_t> last_position{ _past_input->cursor[0], _past_input->cursor[1] };
        numtk::vec2<int32_t> position{ _input->cursor[0], _input->cursor[1] };
        numtk::vec2<int32_t> delta = position - last_position;
        _state->sun_spherical += numtk::vec2f{
            0.01f * (float)delta.x(),
            -0.01f * (float)delta.y(),
        };

        _state->sun_spherical.y() =
            std::min(std::max(_state->sun_spherical.y(), -numtk::kPi*0.4999f), numtk::kPi*0.4999f);

        reset_progressive_counter = true;
    }

    if (!_input->key_down['c'] && _past_input->key_down['c'])
    {
        _state->pixel_cycling_enabled = !_state->pixel_cycling_enabled;
        _state->enable_indirect_light = !_state->enable_indirect_light;
    }

    if (_input->key_down['p'] && !_past_input->key_down['p'])
        _state->region_package.display_depth = std::min(_state->region_package.display_depth+1, _state->region_package.stack_depth);
    if (_input->key_down['o'] && !_past_input->key_down['o'])
        _state->region_package.display_depth = std::max(_state->region_package.display_depth-1, 0);

    numtk::vec3i stencil_delta{ 0, 0, 0 };
    int32_t shift_factor = 1;
    if (_input->mod_down & iotk::fKeyMod::kAlt)
        shift_factor = 10;

    if (_input->key_down['u'] && !_past_input->key_down['u'])
        stencil_delta += numtk::vec3i{ 0, 1, 0 };
    if (_input->key_down['j'] && !_past_input->key_down['j'])
        stencil_delta += numtk::vec3i{ 0, -1, 0 };
    if (_input->key_down['h'] && !_past_input->key_down['h'])
        stencil_delta += numtk::vec3i{ -1, 0, 0 };
    if (_input->key_down['k'] && !_past_input->key_down['k'])
        stencil_delta += numtk::vec3i{ 1, 0, 0 };
    if (_input->key_down['y'] && !_past_input->key_down['y'])
        stencil_delta += numtk::vec3i{ 0, 0, -1 };
    if (_input->key_down['i'] && !_past_input->key_down['i'])
        stencil_delta += numtk::vec3i{ 0, 0, 1 };

    stencil_delta *= shift_factor;

    if (stencil_delta != numtk::vec3i::Constant(0))
    {
        _state->stencil_location += stencil_delta;

#if 1
        _state->voxel_region = _state->initial_region.Shift(_state->stencil_location);
        _state->region_package = UploadVoxelRegion(_vulkan, _state->voxel_region,
                                                   _state->region_package.area_size,
                                                   _state->region_package.node_size);

        SparseRegionUpdateBindings(_state->sparse_runtime);
#else

        _state->stencil_location = numtk::min(numtk::max(_state->stencil_location,
                                                         numtk::vec3i::Constant(-16)),
                                              numtk::vec3i::Constant(16));

        {
            voxtk::BinaryRegion src{ numtk::vec3u{ 16, 16, 16 } };
            for (uint32_t z = 0; z < 16; ++z)
                for (uint32_t y = 0; y < 16; ++y)
                    for (uint32_t x = 0; x < 16; ++x)
                        src.Set(numtk::vec3u{ x, y, z }, !!(((x&1) + (y&1) + (z&1)) & 1));

            voxtk::BinaryRegion mask{ numtk::vec3u{ 3, 3, 3 } };
            //mask.Set({ 0, 0, 0 }, { 3, 3, 3 }, uint8_t(0));
            mask.Set(numtk::bounds3u{{ 0, 0, 0 }, { 3, 3, 3 }}, true);

            //_state->voxel_region = src.BitwiseAnd(mask, _state->stencil_location);
            _state->voxel_region = src.BitwiseAnd(_state->stencil_location.cast<int32_t>(), mask);
            //_state->voxel_region = src.Shift(_state->stencil_location);

            uint32_t area_size = 4u;
            uint32_t node_size = 4u;
            _state->region_package = UploadVoxelRegion(_vulkan, _state->voxel_region, area_size, node_size);

            SparseRegionUpdateBindings(_state->sparse_runtime);
        }
#endif
    }

    if (!_state->current_gui_record->mouse_over)
        _state->camera.Update(_input, _past_input);

    if (_state->camera.updated
        || (old_indirect_light_state != _state->enable_indirect_light)
        || reset_progressive_counter)
        _state->progressive_frame_index = 0;

    return true;
}

void Draw(void* _pstate,
          vktk::Context* _vulkan,
          vktk::Swapchain const& _swapchain,
          VkSemaphore _acquire_semaphore)
{
    voxel_state_t* _state = (voxel_state_t*)_pstate;

    if (_state->pixel_cycling_enabled)
        _state->current_runtime = &_state->pixel_cycling_runtime;
    else
        _state->current_runtime = &_state->forward_runtime;

    vktk::CommandBuffer command_buffer = _vulkan->CreateCommandBuffer(_vulkan->present_queue);
    command_buffer.Begin();

#if defined(TRAVERSAL_V1)
    _state->current_runtime->Dispatch(*_state->current_runtime, command_buffer, _swapchain);
#elif defined(TRAVERSAL_V2)
    //_state->sparse_runtime.Dispatch(_state->sparse_runtime, command_buffer, _swapchain);
    _state->progressive_runtime.Dispatch(_state->progressive_runtime, command_buffer, _swapchain);
#endif

    command_buffer.End();

    VkPipelineStageFlags wait_dst_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    command_buffer.Submit(nullptr, &_acquire_semaphore, &wait_dst_stage, 1u,
                          &_state->last_submit);

    _state->progressive_runtime.PostSubmit(_state->progressive_runtime, _state->last_submit);

#if 0
    _state->sparse_runtime.PostSubmit(_state->sparse_runtime, _state->last_submit);
    if (_state->current_runtime->PostSubmit)
        _state->current_runtime->PostSubmit(*_state->current_runtime, _state->last_submit);
#endif

    _vulkan->ScheduleFenceDestroy(_state->last_submit);

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

    ++_state->frame_index;
    ++_state->progressive_frame_index;
}

void Shutdown(void* _pstate,
              vktk::Context* _vulkan)
{
    voxel_state_t* _state = (voxel_state_t*)_pstate;

    _vulkan->DestroyTexture(_state->region_texture);
    _state->region_texture = vktk::Texture{};

    for (uint32_t index = 0; index < _state->section_textures.size(); ++index)
        _vulkan->DestroyTexture(_state->section_textures[index]);

    _state->sparse_runtime.DestroyPayload(_state->sparse_runtime);

    guitk::DeleteContext(_state->gui_context);
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
        spherical_coordinates.z *= (1.f + 0.0005f * (float)_input->wheel_delta);
        if (spherical_coordinates.z < 0.01f)
            spherical_coordinates.z = 0.01f;
    }

    if (_input->button_down & iotk::fMouseButton::kRightBtn)
    {
        y_offset += 0.0005f * (float)(_input->cursor[1] -  _past_input->cursor[1]);
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

        next_section.x = std::max(0, std::min(31, next_section.x));
        next_section.y = std::max(0, std::min(23, next_section.y));
        next_section.z = std::max(0, std::min(31, next_section.z));

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
        (float)current_section.x + 0.5f,
        (float)current_section.y + 0.5f,
        (float)current_section.z + 0.5f
    } / 16.f) - numtk::vec3f{ 1.f, 1.f, 1.f } + numtk::vec3f{ 0.f, y_offset, 0.f };

    return
        numtk::mat4f::View({ 0.f, 0.f, -1.f },
                           { 0.f, 1.f, 0.f },
                           { 0.f, 0.f, spherical_coordinates.z })
        * numtk::mat4f::View(forward, up, center);

}

void FlightCamera::Update(iotk::input_t const* _input,
                          iotk::input_t const* _past_input)
{
    updated = false;

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

        updated = true;
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
        movement.z += 1.f;
    if (_input->key_down['s'])
        movement.z -= 1.f;
    if (_input->key_down['a'])
        movement.x += 1.f;
    if (_input->key_down['d'])
        movement.x -= 1.f;
    if (_input->key_down[' '])
        movement.y += 1.f;
    if (_input->mod_down & iotk::fKeyMod::kShift)
        movement.y -= 1.f;
    movement.normalise();

    if (movement != numtk::vec3f::Constant(0.f))
        updated = true;

    position += (forward*movement.z + left*movement.x + numtk::vec3f{ 0.f, movement.y, 0.f })
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
    , state{ new voxel_state_t{} }
{
    ReloadPointers();
}

Interface::~Interface()
{
    delete (voxel_state_t*)state;
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

namespace {

vkrg::layout::DispatchStage RegionPassDispatchStageInfo()
{
    vkrg::layout::DispatchStage output{};
    output.name = "render_region";

    output.shaders = std::vector<vkrg::layout::ShaderStage>{
        { VK_SHADER_STAGE_VERTEX_BIT,
          { "#version 450",
            "#define CW_CUBE",
            #include "shaders/cube_vertices.vert.h"
            ,
            #include "shaders/draw_cube.vert.h"
          }
        },
        { VK_SHADER_STAGE_FRAGMENT_BIT,
          { "#version 450",
            #include "shaders/voxel_traversal.h"
            ,
            #include "shaders/render_region.frag.h"
          }
        }
    };

    output.invocation_count = 64;
    output.binding_groups.push_back(vkrg::layout::StageBindingGroup{
        {
            { VK_SHADER_STAGE_ALL, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
              0, 1 },
            { VK_SHADER_STAGE_FRAGMENT_BIT, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
              0, 1 },
            { VK_SHADER_STAGE_FRAGMENT_BIT, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
              VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT, 32*32*32 },
        }
    });

    output.push_constants = std::vector<vkrg::layout::PushConstantRange>{
        { VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(numtk::vec3f) }
    };

    output.graphics.state_overrides = std::vector<vktk::Context::StateOverride>{};
    output.graphics.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    output.graphics.input_layout = std::vector<vktk::Context::VertexInput>{};

    return output;
}

vkrg::layout::DispatchStage SparseRegionDispatchStageInfo()
{
    vkrg::layout::DispatchStage output = {};
    output.name = "sparse_region";

    output.shaders = std::vector<vkrg::layout::ShaderStage>{
        { VK_SHADER_STAGE_VERTEX_BIT,
          { "#version 450",
            "#define CW_CUBE",
            #include "shaders/cube_vertices.vert.h"
            ,
            #include "shaders/draw_cube.vert.h"
          }
        },
        { VK_SHADER_STAGE_FRAGMENT_BIT,
          { "#version 450",
            #include "shaders/voxel_traversal.h"
            ,
            #include "shaders/sparse_region.frag.h"
          }
        }
    };

    output.invocation_count = 1;

    output.binding_groups.push_back(vkrg::layout::StageBindingGroup{
        {
            { VK_SHADER_STAGE_ALL, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
              0, 1 },
        }
    });

    output.binding_groups.push_back(vkrg::layout::StageBindingGroup{
        {
            { VK_SHADER_STAGE_FRAGMENT_BIT, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
              VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT, 512*512 },
        }
    });

    output.binding_groups.push_back(vkrg::layout::StageBindingGroup{
        {
            { VK_SHADER_STAGE_FRAGMENT_BIT, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
              VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT, 512*512 },
        }
    });

    output.binding_groups.push_back(vkrg::layout::StageBindingGroup{
        {
            { VK_SHADER_STAGE_FRAGMENT_BIT, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
              VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT, 512*512 },
        }
    });

    output.push_constants = std::vector<vkrg::layout::PushConstantRange>{
        { VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(SparseRegionPushConstants) }
    };

    output.graphics.state_overrides = std::vector<vktk::Context::StateOverride>{};
    output.graphics.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    output.graphics.input_layout = std::vector<vktk::Context::VertexInput>{};

    return output;
}

vkrg::layout::DispatchStage SkyboxDispatchStageInfo()
{
    vkrg::layout::DispatchStage output = {};
    output.name = "skybox";

    output.shaders = std::vector<vkrg::layout::ShaderStage>{
        { VK_SHADER_STAGE_VERTEX_BIT,
          { "#version 450",
            #include "shaders/full_screen.vert.h"
          }
        },
        { VK_SHADER_STAGE_FRAGMENT_BIT,
          { "#version 450",
            #include "shaders/skybox.frag.h"
          }
        }
    };

    output.invocation_count = 1;
    output.binding_groups = std::vector<vkrg::layout::StageBindingGroup>{};
    output.push_constants = std::vector<vkrg::layout::PushConstantRange>{
        { VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(SkyboxPushConstants) }
    };

    output.graphics.state_overrides = std::vector<vktk::Context::StateOverride>{};
    output.graphics.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    output.graphics.input_layout = std::vector<vktk::Context::VertexInput>{};

    return output;
}

vkrg::layout::DispatchStage ResolveDispatchStageInfo()
{
    vkrg::layout::DispatchStage output = {};
    output.name = "resolve";

    output.shaders = std::vector<vkrg::layout::ShaderStage>{
        { VK_SHADER_STAGE_VERTEX_BIT,
          { "#version 450",
            #include "shaders/full_screen.vert.h"
          }
        },
        { VK_SHADER_STAGE_FRAGMENT_BIT,
          { "#version 450",
            #include "shaders/resolve.frag.h"
          }
        }
    };

    output.invocation_count = 1;
    output.binding_groups.push_back(vkrg::layout::StageBindingGroup{
        {
            { VK_SHADER_STAGE_FRAGMENT_BIT, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
              0, 1 }
        }
    });

    output.push_constants = std::vector<vkrg::layout::PushConstantRange>{
        { VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(numtk::vec2i) }
    };

    output.graphics.state_overrides = std::vector<vktk::Context::StateOverride>{};
    output.graphics.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    output.graphics.input_layout = std::vector<vktk::Context::VertexInput>{};

    return output;
}

vkrg::layout::DispatchStage GUIFramesDispatchStageInfo()
{
    vkrg::layout::DispatchStage output = {};
    output.name = "guiframes";

    output.shaders = std::vector<vkrg::layout::ShaderStage>{
        { VK_SHADER_STAGE_VERTEX_BIT,
          { "#version 450",
            "layout(location = 0) in vec2 iVS_point;"
            "layout(location = 1) in vec2 iVS_uv;"
            "layout(location = 0) out vec2 iFS_point;"
            "layout(push_constant, std430) uniform PushConstants { vec2 viewport_size; };"
            "void main() {"
            "gl_Position = vec4((iVS_point / viewport_size) * vec2(2.0) - vec2(1.0), 0.0, 1.0);"
            "iFS_point = iVS_uv;"
            "}"
          }
        },

        { VK_SHADER_STAGE_FRAGMENT_BIT,
          { "#version 450",
            "layout(location = 0) in vec2 iFS_point;"
            "layout(location = 0) out vec4 color;"
            "layout(push_constant, std430) uniform PushConstants { vec2 viewport_size; };"

            "void main() {"
            "vec2 frame_coords = iFS_point - vec2(0.5);"
            "float border_distance = length(frame_coords);"
            "float border_color = min(smoothstep(0.0, 0.25, border_distance), smoothstep(0.5, 0.25, border_distance));"
            "color = vec4(border_color.xxx, 1.0);"
            "}"
          }
        }
    };

    output.invocation_count = 0;

    output.push_constants = std::vector<vkrg::layout::PushConstantRange>{
        { VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(numtk::vec2f) }
    };

    output.graphics.state_overrides = std::vector<vktk::Context::StateOverride>{
        { vktk::StateOverrideTag::DepthTestEnable, VK_FALSE },
        { vktk::StateOverrideTag::DepthWriteEnable, VK_FALSE }
    };
    output.graphics.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    output.graphics.input_layout = std::vector<vktk::Context::VertexInput>{
        { 0, 0, VK_VERTEX_INPUT_RATE_VERTEX, VK_FORMAT_R32G32_SFLOAT,
          0, sizeof(numtk::vec2f) },
        { 1, 1, VK_VERTEX_INPUT_RATE_VERTEX, VK_FORMAT_R32G32_SFLOAT,
          0, sizeof(numtk::vec2f) }
    };

    return output;
}

vkrg::layout::DispatchStage GUIGlyphsDispatchStageInfo()
{
    vkrg::layout::DispatchStage output = {};
    output.name = "guiglyphs";

    output.shaders = std::vector<vkrg::layout::ShaderStage>{
        { VK_SHADER_STAGE_VERTEX_BIT,
          { "#version 450",
            "layout(location = 0) in vec2 iVS_point;"
            "layout(location = 1) in vec2 iVS_uv;"
            "layout(location = 0) out vec2 iFS_uv;"
            "layout(push_constant, std430) uniform PushConstants { vec2 viewport_size; };"
            "void main() {"
            "gl_Position = vec4((iVS_point / viewport_size) * vec2(2.0) - vec2(1.0), 0.0, 1.0);"
            "iFS_uv = iVS_uv;"
            "}"
          }
        },

        { VK_SHADER_STAGE_FRAGMENT_BIT,
          { "#version 450",
            "layout(location = 0) in vec2 iFS_uv;"
            "layout(location = 0) out vec4 color;"
            "layout(set = 0, binding = 0) uniform usampler2D glyph_atlas;"
            "void main() {"
            "uint glyph_pixel = texture(glyph_atlas, iFS_uv).x;"
            "if (glyph_pixel == 0) discard;"
            "color = vec4(1.0);"
            "}"
          }
        }
    };

    output.invocation_count = 0;
    output.push_constants = std::vector<vkrg::layout::PushConstantRange>{
        { VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(numtk::vec2f) }
    };
    output.binding_groups.push_back(vkrg::layout::StageBindingGroup{
        {
            { VK_SHADER_STAGE_FRAGMENT_BIT, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
              0, 1 }
        }
    });

    output.graphics.state_overrides = std::vector<vktk::Context::StateOverride>{};
    output.graphics.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    output.graphics.input_layout = std::vector<vktk::Context::VertexInput>{
        { 0, 0, VK_VERTEX_INPUT_RATE_VERTEX, VK_FORMAT_R32G32_SFLOAT,
          0, sizeof(numtk::vec2f) },
        { 1, 1, VK_VERTEX_INPUT_RATE_VERTEX, VK_FORMAT_R32G32_SFLOAT,
          0, sizeof(numtk::vec2f) }
    };

    return output;
}

vkrg::layout::DispatchStage ProgressiveResolveStageInfo()
{
    vkrg::layout::DispatchStage output = {};
    output.name = "progressive-resolve";

    output.shaders = std::vector<vkrg::layout::ShaderStage>{
        { VK_SHADER_STAGE_VERTEX_BIT,
          { "#version 450",
            #include "shaders/full_screen.vert.h"
          }
        },
        { VK_SHADER_STAGE_FRAGMENT_BIT,
          { "#version 450",
            "layout(location = 0) in vec3 iFS_csPosition;"
            "layout(location = 0) out vec4 oFS_color;"
            "layout(set = 0, binding = 0) uniform sampler2D framebuffer;"
            "layout(push_constant, std430) uniform PushConstant {"
            "float sample_weight;"
            "};"
            "void main() {"
            "oFS_color = vec4(texelFetch(framebuffer, ivec2(gl_FragCoord.xy), 0).xyz, sample_weight);"
            "}"
          }
        }
    };

    output.invocation_count = 1;
    output.binding_groups.push_back(vkrg::layout::StageBindingGroup{
        {
            { VK_SHADER_STAGE_FRAGMENT_BIT, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
              0, 1 }
        }
    });

    output.push_constants.push_back(vkrg::layout::PushConstantRange{
        VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(float) }
    );

    output.graphics.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    output.graphics.blend_states.push_back(vktk::Context::BlendState{
        VK_TRUE,
        VK_BLEND_FACTOR_SRC_ALPHA,
        VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        VK_BLEND_OP_ADD
    });

    return output;
}

vkrg::layout::DispatchGraph SparseRegionRenderSystem(dstk::Registry& _registry)
{
    vkrg::layout::DispatchGraph graph{ &_registry };

    _registry.BindComponent(graph["FramebufferDispatch"], vkrg::layout::DispatchPass{
        .mode = vkrg::layout::DispatchMode_Graphics,
        .graphics = {
            .attachments = std::vector<vkrg::layout::RenderAttachment>{
                {
                    VK_IMAGE_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM,
                    numtk::vec4f{ 0.f, 1.f, 1.f, 0.f }
                },
                {
                    VK_IMAGE_TYPE_2D, VK_FORMAT_D32_SFLOAT,
                    vkrg::layout::ClearValue{ 1.f, 0u }
                }
            }
        }
    });

    {
        vkrg::layout::DispatchGraph& subpasses = graph.MakeSubgraph("FramebufferDispatch");
        subpasses.BindSubgraph("SparseRegionSubpass", SparseRegionRenderGraph(_registry));
        subpasses.BindSubgraph("GUISubpass", GUIRenderGraph(_registry));
        subpasses.MakeEdge("SparseRegionSubpass", "GUISubpass");
    }

    _registry.BindComponent(graph["present"], vkrg::layout::NodeTag_Present);
    _registry.BindComponent(graph.MakeEdge("FramebufferDispatch", "present"),
                            vkrg::layout::EdgeBindings{
                                vkrg::layout::EdgeType_Image,
                                vkrg::layout::BindingPoint{
                                    .type = vkrg::layout::BindingType_Framebuffer,
                                    .attachment_index = 0
                                },
                                {}
                            });

    return graph;
}

vkrg::layout::DispatchGraph FullFrameRenderGraph(dstk::Registry& _registry)
{
    vkrg::layout::DispatchGraph graph{ &_registry };

    _registry.BindComponent(graph["FramebufferDispatch"], vkrg::layout::DispatchPass{
        .mode = vkrg::layout::DispatchMode_Graphics,
        .graphics = {
            .attachments = {
                {
                    VK_IMAGE_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM,
                    numtk::vec4f{ 0.f, 1.f, 1.f, 0.f }
                },
                {
                    VK_IMAGE_TYPE_2D, VK_FORMAT_D32_SFLOAT,
                    vkrg::layout::ClearValue{ 1.f, 0u }
                }
            }
        }
    });

    {
        vkrg::layout::DispatchGraph& subpasses = graph.MakeSubgraph("FramebufferDispatch");
        _registry.BindComponent(subpasses["RegionPass"], RegionPassDispatchStageInfo());
        _registry.BindComponent(subpasses["SkyboxPass"], SkyboxDispatchStageInfo());
        subpasses.MakeEdge("RegionPass", "SkyboxPass");

        subpasses.BindSubgraph("GUISubpass", GUIRenderGraph(_registry));
        subpasses.MakeEdge("SkyboxPass", "GUISubpass");
    }

    _registry.BindComponent(graph["present"], vkrg::layout::NodeTag_Present);
    _registry.BindComponent(graph.MakeEdge("FramebufferDispatch", "present"),
                            vkrg::layout::EdgeBindings{
                                vkrg::layout::EdgeType_Image,
                                vkrg::layout::BindingPoint{
                                    .type = vkrg::layout::BindingType_Framebuffer,
                                    .attachment_index = 0
                                },
                                {}
                            });

    return graph;
}

vkrg::layout::DispatchGraph PixelCyclingRenderGraph(dstk::Registry& _registry)
{
    vkrg::layout::DispatchGraph graph{ &_registry };

    _registry.BindComponent(graph["FramebufferDispatch"], vkrg::layout::DispatchPass{
        .mode = vkrg::layout::DispatchMode_Graphics,
        .graphics = {
            .attachments = {
                {
                    VK_IMAGE_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM,
                    numtk::vec4f{ 0.f, 1.f, 1.f, 0.f }
                },
                {
                    VK_IMAGE_TYPE_2D, VK_FORMAT_D32_SFLOAT,
                    vkrg::layout::ClearValue{ 1.f, 0u }
                }
            }
        }
    });

    {
        vkrg::layout::DispatchGraph& subpasses = graph.MakeSubgraph("FramebufferDispatch");
        _registry.BindComponent(subpasses["RegionPass"], RegionPassDispatchStageInfo());
        _registry.BindComponent(subpasses["SkyboxPass"], SkyboxDispatchStageInfo());
        subpasses.MakeEdge("RegionPass", "SkyboxPass");
    }

    _registry.BindComponent(graph["FramebufferResolve"], vkrg::layout::DispatchPass{
        .mode = vkrg::layout::DispatchMode_Graphics,
        .graphics = {
            .attachments = {
                {
                    VK_IMAGE_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM,
                    numtk::vec4f{ 0.f, 1.f, 1.f, 0.f }
                },
                {
                    VK_IMAGE_TYPE_2D, VK_FORMAT_D32_SFLOAT,
                    vkrg::layout::ClearValue{ 1.f, 0u }
                }
            }
        }
    });

    {
        vkrg::layout::DispatchGraph& subpasses = graph.MakeSubgraph("FramebufferResolve");
        _registry.BindComponent(subpasses["ResolvePass"], ResolveDispatchStageInfo());

        subpasses.BindSubgraph("GUISubpass", GUIRenderGraph(_registry));
        subpasses.MakeEdge("ResolvePass", "GUISubpass");
    }

    _registry.BindComponent(graph.MakeEdge("FramebufferDispatch", "FramebufferResolve"),
                            vkrg::layout::EdgeBindings{
                                vkrg::layout::EdgeType_Image,
                                vkrg::layout::BindingPoint{
                                    .type = vkrg::layout::BindingType_Framebuffer,
                                    .attachment_index = 0
                                },
                                vkrg::layout::BindingPoint{
                                    .type = vkrg::layout::BindingType_Shader,
                                    .node_override = graph["FramebufferResolve"]["ResolvePass"],
                                    .shader = { 0, 0 }
                                }
                            });

    _registry.BindComponent(graph["present"], vkrg::layout::NodeTag_Present);
    _registry.BindComponent(graph.MakeEdge("FramebufferResolve", "present"),
                            vkrg::layout::EdgeBindings{
                                vkrg::layout::EdgeType_Image,
                                vkrg::layout::BindingPoint{
                                    .type = vkrg::layout::BindingType_Framebuffer,
                                    .attachment_index = 0
                                },
                                {}
                            });

    return graph;
}

vkrg::layout::DispatchGraph ProgressiveRenderGraph(dstk::Registry& _registry)
{
    vkrg::layout::DispatchGraph graph{ &_registry };

    _registry.BindComponent(graph["FramebufferDispatch"], vkrg::layout::DispatchPass{
        .mode = vkrg::layout::DispatchMode_Graphics,
        .graphics = {
            .attachments = {
                {
                    VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT,
                    numtk::vec4f{ 0.f, 1.f, 1.f, 0.f }
                },
                {
                    VK_IMAGE_TYPE_2D, VK_FORMAT_D32_SFLOAT,
                    vkrg::layout::ClearValue{ 1.f, 0u }
                }
            }
        }
    });

    {
        vkrg::layout::DispatchGraph& subpasses = graph.MakeSubgraph("FramebufferDispatch");
        subpasses.BindSubgraph("SparseRegionSubpass", SparseRegionRenderGraph(_registry));
    }

    _registry.BindComponent(graph["FramebufferResolve"], vkrg::layout::DispatchPass{
        .mode = vkrg::layout::DispatchMode_Graphics,
        .graphics = {
            .attachments = {
                { VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT,
                  numtk::vec4f{ 0.f, 0.f, 0.f, 0.f }, VK_ATTACHMENT_LOAD_OP_LOAD }
            }
        }
    });
    graph.MakeSubgraph("FramebufferResolve");
    _registry.BindComponent(graph["FramebufferResolve"]["ResolvePass"],
                            ProgressiveResolveStageInfo());

    _registry.BindComponent(graph.MakeEdge("FramebufferDispatch", "FramebufferResolve"),
                            vkrg::layout::EdgeBindings{
                                vkrg::layout::EdgeType_Image,
                                vkrg::layout::BindingPoint{
                                    .type = vkrg::layout::BindingType_Framebuffer,
                                    .attachment_index = 0
                                },
                                vkrg::layout::BindingPoint{
                                    .type = vkrg::layout::BindingType_Shader,
                                    .node_override = graph["FramebufferResolve"]["ResolvePass"],
                                    .shader = { 0, 0 }
                                }
                            });

    _registry.BindComponent(graph["FramebufferBlit"], vkrg::layout::NodeTag_BlitImage);
    _registry.BindComponent(graph.MakeEdge("FramebufferResolve", "FramebufferBlit"),
                            vkrg::layout::EdgeBindings{
                                vkrg::layout::EdgeType_Image,
                                vkrg::layout::BindingPoint{
                                    .type = vkrg::layout::BindingType_Framebuffer,
                                    .attachment_index = 0
                                },
                                vkrg::layout::BindingPoint{
                                    .type = vkrg::layout::BindingType_BlitImage,
                                    .blit_slot = vkrg::layout::BlitImageSlot_Source
                                }
                            });

    _registry.BindComponent(graph["swapchain_input"], vkrg::layout::NodeTag_Input);
    _registry.BindComponent(graph.MakeEdge("swapchain_input", "FramebufferBlit"),
                            vkrg::layout::EdgeBindings{
                                vkrg::layout::EdgeType_Image,
                                {},
                                vkrg::layout::BindingPoint{
                                    .type = vkrg::layout::BindingType_BlitImage,
                                    .blit_slot = vkrg::layout::BlitImageSlot_Sink
                                }
                            });

    _registry.BindComponent(graph["FramebufferScreenSpace"], vkrg::layout::DispatchPass{
        .mode = vkrg::layout::DispatchMode_Graphics,
        .graphics = {
            .attachments = {
                {
                    VK_IMAGE_TYPE_2D, VK_FORMAT_B8G8R8A8_SRGB,
                    numtk::vec4f::Constant(0.f), VK_ATTACHMENT_LOAD_OP_LOAD
                }
            }
        }
    });

    {
        vkrg::layout::DispatchGraph& subpasses = graph.MakeSubgraph("FramebufferScreenSpace");
        subpasses.BindSubgraph("GUISubpass", GUIRenderGraph(_registry));
    }

    _registry.BindComponent(graph.MakeEdge("FramebufferBlit", "FramebufferScreenSpace"),
                            vkrg::layout::EdgeBindings{
                                vkrg::layout::EdgeType_Image,
                                vkrg::layout::BindingPoint{
                                    .type = vkrg::layout::BindingType_BlitImage,
                                    .blit_slot = vkrg::layout::BlitImageSlot_Sink
                                },
                                vkrg::layout::BindingPoint{
                                    .type = vkrg::layout::BindingType_Framebuffer,
                                    .attachment_index = 0
                                }
                            });

    _registry.BindComponent(graph["present"], vkrg::layout::NodeTag_Present);
    _registry.BindComponent(graph.MakeEdge("FramebufferScreenSpace", "present"),
                            vkrg::layout::EdgeBindings{
                                vkrg::layout::EdgeType_Image,
                                vkrg::layout::BindingPoint{
                                    .type = vkrg::layout::BindingType_Framebuffer,
                                    .attachment_index = 0
                                },
                                {}
                            });

    return graph;
}

vkrg::layout::DispatchGraph SparseRegionRenderGraph(dstk::Registry& _registry)
{
    vkrg::layout::DispatchGraph output{ &_registry };
    _registry.BindComponent(output["SparseRegion"], SparseRegionDispatchStageInfo());
    return output;
}

vkrg::layout::DispatchGraph GUIRenderGraph(dstk::Registry& _registry)
{
    vkrg::layout::DispatchGraph output{ &_registry };
    _registry.BindComponent(output["GUIFrames"], GUIFramesDispatchStageInfo());
    _registry.BindComponent(output["GUIGlyphs"], GUIGlyphsDispatchStageInfo());
    output.MakeEdge("GUIFrames", "GUIGlyphs");
    return output;
}

struct DensePipelinePayload
{
    vktk::Buffer matrix_buffer;
    vktk::Buffer staging_buffer;
    VkSampler nearest_sampler;
};

dispatch_runtime_t DenseRenderingPipeline(
    voxel_state_t* _state, vktk::Context* _vulkan)
{
    dispatch_runtime_t output = {};
    output.graph = FullFrameRenderGraph(_state->core_registry);
    output.vulkan = _vulkan;
    output.registry = &_state->core_registry;
    output.state = _state;
    output.payload = new DensePipelinePayload{};

    BindGUIRuntime(_state, _vulkan, output.graph["FramebufferDispatch"]["GUISubpass"]);

    output.ConstantResources = [](dispatch_runtime_t& _rt)
    {
        _rt.graph.DepthFirstTraversal(
            [&]
            (uint32_t, dstk::Registry::Node node) {
                dispatch_runtime_t* runtime =
                    _rt.registry->ComponentLookup<dispatch_runtime_t>(node);
                if (runtime && runtime->ConstantResources)
                    runtime->ConstantResources(*runtime);
            });

        DensePipelinePayload& payload = *(DensePipelinePayload*)_rt.payload;

        vkrg::bind::PNextMap pNext_map = {};

        static uint32_t desc_count = 0u;
        static VkDescriptorSetVariableDescriptorCountAllocateInfo desc_count_alloc = {};
        desc_count = (uint32_t)_rt.state->section_textures.size();
        desc_count_alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO;
        desc_count_alloc.pNext = nullptr;
        desc_count_alloc.descriptorSetCount = 1;
        desc_count_alloc.pDescriptorCounts = &desc_count;
        void* pnext_begin = &desc_count_alloc;

        pNext_map.emplace("RegionPass", std::vector<vkrg::bind::PNextEntry>{
            vkrg::bind::PNextEntry{ 0, (void*)&desc_count_alloc }
        });

        _rt.Context().GenerateStaticRuntime();
        _rt.Context().ClearDynamicRuntime();
        _rt.Context().ResetDynamicRuntime(pNext_map);

        payload.staging_buffer = _rt.vulkan->CreateBuffer(
            sizeof(MatrixBlock) * 64,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
            | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        payload.matrix_buffer = _rt.vulkan->CreateBuffer(
            sizeof(MatrixBlock) * 64,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT
            | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);

        MatrixBlock* matrices = (MatrixBlock*)payload.staging_buffer.alloc.map;
        for (uint32_t index = 0; index < 64; ++index)
        {
            matrices[index].projection = numtk::mat4f::Constant(0.f);
            matrices[index].view_model = numtk::mat4f::Constant(0.f);
            matrices[index].view = numtk::mat4f::Constant(0.f);
            matrices[index].screen_size = numtk::vec2<int32_t>::Constant(1);
        }

        {
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
            CHECKCALL(vkCreateSampler, _rt.vulkan->device,
                      &sampler_create, nullptr, &payload.nearest_sampler);
        }

        VkDescriptorImageInfo region_image_info{};
        region_image_info.sampler = payload.nearest_sampler;
        region_image_info.imageView = _rt.state->region_texture.view;
        region_image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        dstk::Registry::Node main_world_node =
            _rt.graph["FramebufferDispatch"]["RegionPass"];

        for (uint32_t index = 0; index < 64; ++index)
        {
            std::vector<VkWriteDescriptorSet> desc_writes = {};

            VkWriteDescriptorSet region_binding =
                _rt.Context().BindShaderResource(
                    main_world_node,
                    vkrg::layout::BindingPoint{
                        .type = vkrg::layout::BindingType_Shader,
                        .shader = { 0, 1 }
                    },
                    index,
                    vkrg::bind::ShaderResources{
                        .images = { region_image_info }
                    });

            VkDescriptorBufferInfo buffer_info = {};
            buffer_info.buffer = payload.matrix_buffer;
            buffer_info.offset = sizeof(MatrixBlock) * index;
            buffer_info.range = sizeof(MatrixBlock);
            VkWriteDescriptorSet matrix_binding =
                _rt.Context().BindShaderResource(
                    main_world_node,
                    vkrg::layout::BindingPoint{
                        .type = vkrg::layout::BindingType_Shader,
                        .shader = { 0, 0 }
                    },
                    index,
                    vkrg::bind::ShaderResources{
                        .buffers = { buffer_info }
                    });

            std::vector<VkDescriptorImageInfo> section_images_info(
                _rt.state->section_textures.size());
            for (uint32_t section_index = 0u;
                 section_index < _rt.state->section_textures.size();
                 ++section_index)
            {
                VkDescriptorImageInfo& image_info = section_images_info[section_index];
                image_info.sampler = payload.nearest_sampler;
                image_info.imageView = _rt.state->section_textures[section_index].view;
                image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            }

            VkWriteDescriptorSet section_bindings =
                _rt.Context().BindShaderResource(
                    main_world_node,
                    vkrg::layout::BindingPoint{
                        .type = vkrg::layout::BindingType_Shader,
                        .shader = { 0, 2 }
                    },
                    index,
                    vkrg::bind::ShaderResources{
                        .images = section_images_info
                    });

            if (region_image_info.imageView)
                desc_writes.push_back(region_binding);
            desc_writes.push_back(matrix_binding);
            if (!section_images_info.empty())
                desc_writes.push_back(section_bindings);

            vkUpdateDescriptorSets(_rt.vulkan->device, desc_writes.size(), desc_writes.data(), 0u, nullptr);
        }

        vktk::CommandBuffer command_buffer =
            _rt.vulkan->CreateCommandBuffer(_rt.vulkan->present_queue);
        command_buffer.Begin();

        command_buffer.BufferTransitions(
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            {
                { payload.matrix_buffer,
                  VK_ACCESS_NONE,
                  VK_ACCESS_TRANSFER_WRITE_BIT },
            });

        command_buffer.BufferCopy(payload.matrix_buffer, 0, payload.staging_buffer, 0,
                                  sizeof(MatrixBlock) * 64);

        command_buffer.BufferTransitions(
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
            {
                { payload.matrix_buffer,
                  VK_ACCESS_TRANSFER_WRITE_BIT,
                  VK_ACCESS_UNIFORM_READ_BIT }
            });

        command_buffer.End();
        command_buffer.Submit();
    };

    output.RenderResources = [](dispatch_runtime_t& _rt,
                                vktk::Swapchain const& _swapchain)
    {
        _rt.graph.DepthFirstTraversal(
            [&]
            (uint32_t, dstk::Registry::Node node) {
                dispatch_runtime_t* runtime =
                    _rt.registry->ComponentLookup<dispatch_runtime_t>(node);
                if (runtime && runtime->RenderResources)
                    runtime->RenderResources(*runtime, _swapchain);
            });

        _rt.Context().ClearFramebufferRuntime();
        _rt.Context().GenerateFramebufferRuntime(&_swapchain.format);
        _rt.Context().BindSwapchain(_rt.graph["FramebufferDispatch"], _swapchain);
    };

    output.Update = [](dispatch_runtime_t& _rt)
    {
        _rt.graph.DepthFirstTraversal(
            [&]
            (uint32_t, dstk::Registry::Node node) {
                dispatch_runtime_t* runtime =
                    _rt.registry->ComponentLookup<dispatch_runtime_t>(node);
                if (runtime && runtime->Update)
                    runtime->Update(*runtime);
            });
    };

    output.Dispatch = [](dispatch_runtime_t& _rt,
                         vktk::CommandBuffer& _command_buffer,
                         vktk::Swapchain const& _swapchain)
    {
        _rt.graph.DepthFirstTraversal(
            [&]
            (uint32_t, dstk::Registry::Node node) {
                dispatch_runtime_t* runtime =
                    _rt.registry->ComponentLookup<dispatch_runtime_t>(node);
                if (runtime && runtime->Dispatch)
                    runtime->Dispatch(*runtime, _command_buffer, _swapchain);
            });

        DensePipelinePayload& payload = *(DensePipelinePayload*)_rt.payload;

        float how = (float)_swapchain.size.height / (float)_swapchain.size.width;
        numtk::vec2u sample_index{
            (_rt.state->frame_index%4)/2,
            _rt.state->frame_index%2 };
        numtk::bounds2u scissor{ { 0, 0 }, { _swapchain.size.width, _swapchain.size.height } };
        numtk::bounds2f viewport{ { 0.f, 0.f }, { (float)scissor.extent.x(), (float)scissor.extent.y() } };

        if (_rt.state->pixel_cycling_enabled)
        {
            scissor = numtk::bounds2u{
                { ((_swapchain.size.width) / 2) * sample_index.x(),
                  ((_swapchain.size.height) / 2) * sample_index.y() },
                { (_swapchain.size.width) / 2,
                  (_swapchain.size.height) / 2 },
            };

            viewport = numtk::bounds2f{
                { 0.f + float(sample_index.x()) * ((float)scissor.extent.x() - 0.5f),
                  0.f + float(sample_index.y()) * ((float)scissor.extent.y() - 0.5f) },
                { (float)scissor.extent.x(),
                  (float)scissor.extent.y() }
            };
        }

        MatrixBlock* matrices = (MatrixBlock*)payload.staging_buffer.alloc.map;
        for (uint32_t index = 0; index < 64; ++index)
        {
            matrices[index].view = _rt.state->camera.Matrix();

            matrices[index].projection =
                numtk::mat4f::Perspective_VulkanNDC(0.1f, 1000.f, 90.f*numtk::kDegrees, how);

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

            matrices[index].screen_offset = viewport.min;
            matrices[index].screen_size = scissor.extent.cast<int32_t>();
        }

        _command_buffer.BufferTransitions(
            VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            {
                { payload.matrix_buffer, VK_ACCESS_UNIFORM_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT }
            });

        _command_buffer.BufferCopy(payload.matrix_buffer, 0,
                                   payload.staging_buffer, 0,
                                   sizeof(MatrixBlock) * 64);

        _command_buffer.BufferTransitions(
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
            {
                { payload.matrix_buffer, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_UNIFORM_READ_BIT }
            });

        numtk::vec3f sun_dir = numtk::vec3f{
            std::cos(_rt.state->sun_spherical[1]) * std::cos(_rt.state->sun_spherical[0]),
            std::sin(_rt.state->sun_spherical[1]),
            std::cos(_rt.state->sun_spherical[1]) * std::sin(_rt.state->sun_spherical[0])
        }.normalise();

        SkyboxPushConstants skybox_params = {};
        skybox_params.projection = numtk::mat4f::Perspective_VulkanNDC(0.1f, 100.f, 90.f*numtk::kDegrees, how);
        skybox_params.view = _rt.state->camera.Matrix();
        skybox_params.position = numtk::vec4f{ 0.f, 20.f, 0.f };
        skybox_params.light_dir = sun_dir;

        dstk::Registry::Node main_world_node =
            _rt.graph["FramebufferDispatch"]["RegionPass"];
        dstk::Registry::Node skybox_node =
            _rt.graph["FramebufferDispatch"]["SkyboxPass"];

        _rt.Context().BindFramebufferArea(
            _rt.graph["FramebufferDispatch"], {
                !_rt.state->pixel_cycling_enabled
                ? _swapchain.index
                : 0,
                scissor,
                viewport
            });

        {
            vkrg::bind::GraphicsCommand command = {};
            command.command = vkrg::bind::DispatchCommand_Draw;
            command.vertex_count = 36;
            command.instance_count = 1;

            for (uint32_t index = 0; index < 64; ++index)
            {
                _rt.Context().BindGraphicsCommand(main_world_node, index, command);

                numtk::vec3f* push_constants =
                    (numtk::vec3f*)_rt.Context().BindPushConstants(
                        main_world_node,
                        {
                            .type = vkrg::layout::BindingType_PushConstantRange,
                            .push_constant_range = 0
                        }, index);
                *push_constants = sun_dir;
            }
        }

        {
            vkrg::bind::GraphicsCommand command = {};
            command.command = vkrg::bind::DispatchCommand_Draw;
            command.vertex_count = 3;
            command.instance_count = 1;

            _rt.Context().BindGraphicsCommand(skybox_node, 0, command);
            SkyboxPushConstants* push_constants =
                (SkyboxPushConstants*)_rt.Context().BindPushConstants(
                    skybox_node,
                    {
                        .type = vkrg::layout::BindingType_PushConstantRange,
                        .push_constant_range = 0
                    }, 0);
            *push_constants = skybox_params;
        }

        if (_rt.state->pixel_cycling_enabled)
        {
            dstk::Registry::Node resolve_node =
                _rt.graph["FramebufferResolve"]["ResolvePass"];

            vkrg::bind::GraphicsCommand command = {};
            command.command = vkrg::bind::DispatchCommand_Draw;
            command.vertex_count = 3;
            command.instance_count = 1;

            _rt.Context().BindGraphicsCommand(resolve_node, 0, command);
            _rt.Context().BindFramebufferArea(
                _rt.graph["FramebufferResolve"], {
                    _swapchain.index,
                    numtk::bounds2u{
                        { 0, 0 },
                        { _swapchain.size.width, _swapchain.size.height }
                    },
                    numtk::bounds2f{
                        { 0.f, 0.f },
                        { (float)_swapchain.size.width, (float)_swapchain.size.height }
                    }
                });
        }

        _rt.Context().Dispatch(_command_buffer);
    };

    output.PostSubmit = [](dispatch_runtime_t& _rt, VkFence _last_submit)
    {
        _rt.graph.DepthFirstTraversal(
            [&]
            (uint32_t, dstk::Registry::Node node) {
                dispatch_runtime_t* runtime =
                    _rt.registry->ComponentLookup<dispatch_runtime_t>(node);
                if (runtime && runtime->PostSubmit)
                    runtime->PostSubmit(*runtime, _last_submit);
            });
    };

    return output;
}

struct DensePixelCyclingPipelinePayload
{
    DensePipelinePayload base;

    vktk::Texture primary_pass_color;
    vktk::Texture primary_pass_depth;
};

dispatch_runtime_t DensePixelCyclingRenderingPipeline(
    voxel_state_t* _state, vktk::Context* _vulkan)
{
    dispatch_runtime_t output = {};
    output.graph = PixelCyclingRenderGraph(_state->core_registry);
    output.vulkan = _vulkan;
    output.registry = &_state->core_registry;
    output.state = _state;
    output.payload = new DensePixelCyclingPipelinePayload{};

    BindGUIRuntime(_state, _vulkan, output.graph["FramebufferResolve"]["GUISubpass"]);

    output.ConstantResources = _state->forward_runtime.ConstantResources;
    output.RenderResources = [](dispatch_runtime_t& _rt, vktk::Swapchain const& _swapchain)
    {
        _rt.graph.DepthFirstTraversal(
            [&]
            (uint32_t, dstk::Registry::Node node) {
                dispatch_runtime_t* runtime =
                    _rt.registry->ComponentLookup<dispatch_runtime_t>(node);
                if (runtime && runtime->RenderResources)
                    runtime->RenderResources(*runtime, _swapchain);
            });

        DensePixelCyclingPipelinePayload& payload = *(DensePixelCyclingPipelinePayload*)_rt.payload;

        _rt.Context().ClearFramebufferRuntime();
        _rt.Context().GenerateFramebufferRuntime(&_swapchain.format);

        {
            _rt.vulkan->DestroyTexture(payload.primary_pass_color);
            payload.primary_pass_color = _rt.vulkan->CreateTexture(
                _swapchain.size.width, _swapchain.size.height, 1,
                VK_IMAGE_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
                | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
                | VK_IMAGE_USAGE_SAMPLED_BIT
            );

            _rt.vulkan->DestroyTexture(payload.primary_pass_depth);
            payload.primary_pass_depth = _rt.vulkan->CreateTexture(
                _swapchain.size.width, _swapchain.size.height, 1,
                VK_IMAGE_TYPE_2D, VK_FORMAT_D32_SFLOAT,
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
            );
        }

        numtk::vec2u size = { _swapchain.size.width, _swapchain.size.height };
        _rt.Context().BindAttachments(
            _rt.graph["FramebufferDispatch"], size,
            { payload.primary_pass_color.view, payload.primary_pass_depth.view }
        );

        _rt.Context().BindSwapchain(_rt.graph["FramebufferResolve"], _swapchain);

        VkDescriptorImageInfo image_info{};
        image_info.sampler = payload.base.nearest_sampler;
        image_info.imageView = payload.primary_pass_color.view;
        image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet desc_write = _rt.Context().BindShaderResource(
            _rt.graph["FramebufferResolve"]["ResolvePass"],
            vkrg::layout::BindingPoint{
                .type = vkrg::layout::BindingType_Shader,
                .shader = { 0, 0 }
            },
            0,
            vkrg::bind::ShaderResources{
                .images = { image_info }
            });

        vkUpdateDescriptorSets(_rt.vulkan->device, 1, &desc_write, 0u, nullptr);

        numtk::vec2i screen_size{ (int32_t)_swapchain.size.width, (int32_t)_swapchain.size.height };
        numtk::vec2i* push_constants =
            (numtk::vec2i*)_rt.Context().BindPushConstants(
                _rt.graph["FramebufferResolve"]["ResolvePass"],
                {
                    .type = vkrg::layout::BindingType_PushConstantRange,
                    .push_constant_range = 0
                }, 0);
        *push_constants = screen_size;

        {
            vktk::CommandBuffer layout_commands =
                _rt.vulkan->CreateCommandBuffer(_rt.vulkan->present_queue);
            layout_commands.Begin();

            layout_commands.ImageTransitions(
                VK_PIPELINE_STAGE_NONE,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                {
                    {
                        payload.primary_pass_color.image,
                        VK_ACCESS_NONE, VK_ACCESS_NONE,
                        VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_IMAGE_ASPECT_COLOR_BIT
                    },
                    {
                        payload.primary_pass_depth.image,
                        VK_ACCESS_NONE, VK_ACCESS_NONE,
                        VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                        VK_IMAGE_ASPECT_DEPTH_BIT
                    }
                });

            layout_commands.End();
            layout_commands.Submit();
        }
    };

    output.Update = [](dispatch_runtime_t& _rt)
    {
        _rt.graph.DepthFirstTraversal(
            [&]
            (uint32_t, dstk::Registry::Node node) {
                dispatch_runtime_t* runtime =
                    _rt.registry->ComponentLookup<dispatch_runtime_t>(node);
                if (runtime && runtime->Update)
                    runtime->Update(*runtime);
            });
    };

    output.Dispatch = _state->forward_runtime.Dispatch;

    output.PostSubmit = [](dispatch_runtime_t& _rt, VkFence _last_submit)
    {
        _rt.graph.DepthFirstTraversal(
            [&]
            (uint32_t, dstk::Registry::Node node) {
                dispatch_runtime_t* runtime =
                    _rt.registry->ComponentLookup<dispatch_runtime_t>(node);
                if (runtime && runtime->PostSubmit)
                    runtime->PostSubmit(*runtime, _last_submit);
            });
    };

    return output;
}

struct SparsePipelinePayload
{
    VkSampler nearest_sampler;
    vktk::Buffer matrix_buffer;
    vktk::Buffer staging_buffer;
};

dispatch_runtime_t SparseRenderingPipeline(
    voxel_state_t* _state, vktk::Context* _vulkan)
{
    dispatch_runtime_t output = {};
    output.graph = SparseRegionRenderSystem(_state->core_registry);
    output.vulkan = _vulkan;
    output.registry = &_state->core_registry;
    output.state = _state;
    output.payload = new SparsePipelinePayload{};

    BindGUIRuntime(_state, _vulkan,
                   output.graph["FramebufferDispatch"]["GUISubpass"]);
    BindSparseRegionRuntime(_state, _vulkan,
                            output.graph["FramebufferDispatch"]["SparseRegionSubpass"]);

    output.DestroyPayload = [](dispatch_runtime_t& _rt)
    {
        _rt.graph.DepthFirstTraversal(
            [&]
            (uint32_t, dstk::Registry::Node node) {
                dispatch_runtime_t* runtime =
                    _rt.registry->ComponentLookup<dispatch_runtime_t>(node);
                if (runtime && runtime->DestroyPayload)
                    runtime->DestroyPayload(*runtime);
            });
    };

    output.ConstantResources = [](dispatch_runtime_t& _rt)
    {
        _rt.Context().GenerateStaticRuntime();
        _rt.Context().ResetDynamicRuntime();

        _rt.graph.DepthFirstTraversal(
            [&]
            (uint32_t, dstk::Registry::Node node) {
                dispatch_runtime_t* runtime =
                    _rt.registry->ComponentLookup<dispatch_runtime_t>(node);
                if (runtime && runtime->ConstantResources)
                    runtime->ConstantResources(*runtime);
            });
    };

    output.RenderResources = [](dispatch_runtime_t& _rt,
                                vktk::Swapchain const& _swapchain)
    {
        _rt.Context().ClearFramebufferRuntime();
        _rt.Context().GenerateFramebufferRuntime(&_swapchain.format);
        _rt.Context().BindSwapchain(_rt.graph["FramebufferDispatch"],
                                    _swapchain);

        _rt.graph.DepthFirstTraversal(
            [&]
            (uint32_t, dstk::Registry::Node node) {
                dispatch_runtime_t* runtime =
                    _rt.registry->ComponentLookup<dispatch_runtime_t>(node);
                if (runtime && runtime->RenderResources)
                    runtime->RenderResources(*runtime, _swapchain);
            });
    };

    output.Update = [](dispatch_runtime_t& _rt)
    {
        _rt.graph.DepthFirstTraversal(
            [&]
            (uint32_t, dstk::Registry::Node node) {
                dispatch_runtime_t* runtime =
                    _rt.registry->ComponentLookup<dispatch_runtime_t>(node);
                if (runtime && runtime->Update)
                    runtime->Update(*runtime);
            });
    };

    output.Dispatch = [](dispatch_runtime_t& _rt,
                         vktk::CommandBuffer& _command_buffer,
                         vktk::Swapchain const& _swapchain)
    {
        _rt.graph.DepthFirstTraversal(
            [&]
            (uint32_t, dstk::Registry::Node node) {
                dispatch_runtime_t* runtime =
                    _rt.registry->ComponentLookup<dispatch_runtime_t>(node);
                if (runtime && runtime->Dispatch)
                    runtime->Dispatch(*runtime, _command_buffer, _swapchain);
            });

        _rt.Context().BindFramebufferArea(
            _rt.graph["FramebufferDispatch"],
            {
                _swapchain.index,
                {
                    { 0, 0 },
                    { _swapchain.size.width, _swapchain.size.height }
                },
                {
                    { 0.f, 0.f },
                    { (float)_swapchain.size.width, (float)_swapchain.size.height }
                }
            });

        _rt.Context().Dispatch(_command_buffer);
    };

    output.PostSubmit = [](dispatch_runtime_t& _rt, VkFence _last_submit)
    {
        _rt.graph.DepthFirstTraversal(
            [&]
            (uint32_t, dstk::Registry::Node node) {
                dispatch_runtime_t* runtime =
                    _rt.registry->ComponentLookup<dispatch_runtime_t>(node);
                if (runtime && runtime->PostSubmit)
                    runtime->PostSubmit(*runtime, _last_submit);
            });
    };

    return output;
}

void SparseRegionUpdateBindings(dispatch_runtime_t& _rt)
{
    SparsePipelinePayload& payload = *(SparsePipelinePayload*)_rt.payload;

    {
        vkrg::bind::PNextMap pNext_map = {};

        static uint32_t desc_counts[3] = { 0u, 0u, 0u };
        desc_counts[0] = (uint32_t)_rt.state->region_package.masks.size();
        desc_counts[1] = (uint32_t)_rt.state->region_package.offsets.size();
        desc_counts[2] = (uint32_t)_rt.state->region_package.payloads.size();

        static VkDescriptorSetVariableDescriptorCountAllocateInfo desc_count_allocs[3];
        desc_count_allocs[0] = VkDescriptorSetVariableDescriptorCountAllocateInfo{};
        desc_count_allocs[1] = VkDescriptorSetVariableDescriptorCountAllocateInfo{};
        desc_count_allocs[2] = VkDescriptorSetVariableDescriptorCountAllocateInfo{};

        desc_count_allocs[0].sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO;
        desc_count_allocs[0].pNext = nullptr;
        desc_count_allocs[0].descriptorSetCount = 1;
        desc_count_allocs[0].pDescriptorCounts = &desc_counts[0];

        desc_count_allocs[1].sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO;
        desc_count_allocs[1].pNext = nullptr;
        desc_count_allocs[1].descriptorSetCount = 1;
        desc_count_allocs[1].pDescriptorCounts = &desc_counts[1];

        desc_count_allocs[2].sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO;
        desc_count_allocs[2].pNext = nullptr;
        desc_count_allocs[2].descriptorSetCount = 1;
        desc_count_allocs[2].pDescriptorCounts = &desc_counts[2];

        pNext_map.emplace("SparseRegion", std::vector<vkrg::bind::PNextEntry>{
            { 1, (void*)&desc_count_allocs[0] },
            { 2, (void*)&desc_count_allocs[1] },
            { 3, (void*)&desc_count_allocs[2] }
        });

        _rt.Context().ResetDynamicRuntime(pNext_map);
    }

    dstk::Registry::Node sparse_region_node = _rt.graph["SparseRegion"];

    std::vector<VkDescriptorImageInfo> nodes_info{};
    std::vector<VkDescriptorImageInfo> offsets_info{};
    std::vector<VkDescriptorImageInfo> payloads_info{};

    for (vktk::Texture const& node : _rt.state->region_package.masks)
    {
        VkDescriptorImageInfo region_image_info{};
        region_image_info.sampler = payload.nearest_sampler;
        region_image_info.imageView = node.view;
        region_image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        nodes_info.push_back(region_image_info);
    }

    for (vktk::Texture const& node_payload : _rt.state->region_package.payloads)
    {
        VkDescriptorImageInfo payload_image_info{};
        payload_image_info.sampler = payload.nearest_sampler;
        payload_image_info.imageView = node_payload.view;
        payload_image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        payloads_info.push_back(payload_image_info);
    }

    for (vktk::Texture const& offsets : _rt.state->region_package.offsets)
    {
        VkDescriptorImageInfo offsets_image_info{};
        offsets_image_info.sampler = payload.nearest_sampler;
        offsets_image_info.imageView = offsets.view;
        offsets_image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        offsets_info.push_back(offsets_image_info);
    }

    VkDescriptorBufferInfo matrix_buffer_info = {};
    matrix_buffer_info.buffer = payload.matrix_buffer;
    matrix_buffer_info.offset = 0;
    matrix_buffer_info.range = sizeof(MatrixBlock);

    std::vector<VkWriteDescriptorSet> desc_writes = {};
    desc_writes.push_back(
        _rt.Context().BindShaderResource(
            sparse_region_node,
            vkrg::layout::BindingPoint{
                .type = vkrg::layout::BindingType_Shader,
                .shader = { 0, 0 }
            },
            0,
            vkrg::bind::ShaderResources{
                .buffers = { matrix_buffer_info }
            })
    );

    desc_writes.push_back(
        _rt.Context().BindShaderResource(
            sparse_region_node,
            vkrg::layout::BindingPoint{
                .type = vkrg::layout::BindingType_Shader,
                .shader = { 1, 0 }
            },
            0,
            vkrg::bind::ShaderResources{
                .images = nodes_info
            })
    );

    desc_writes.push_back(
        _rt.Context().BindShaderResource(
            sparse_region_node,
            vkrg::layout::BindingPoint{
                .type = vkrg::layout::BindingType_Shader,
                .shader = { 2, 0 }
            },
            0,
            vkrg::bind::ShaderResources{
                .images = offsets_info
            })
    );

    desc_writes.push_back(
        _rt.Context().BindShaderResource(
            sparse_region_node,
            vkrg::layout::BindingPoint{
                .type = vkrg::layout::BindingType_Shader,
                .shader = { 3, 0 }
            },
            0,
            vkrg::bind::ShaderResources{
                .images = payloads_info
            })
    );

    vkUpdateDescriptorSets(_rt.vulkan->device, desc_writes.size(), desc_writes.data(), 0u, nullptr);
}

struct ProgressivePipelinePayload
{
    vktk::Texture primary_pass_color;
    vktk::Texture primary_pass_depth;
    vktk::Texture accumulated_color;
    VkSampler nearest_sampler;
};

dispatch_runtime_t ProgressiveRenderingPipeline(voxel_state_t* _state, vktk::Context* _vulkan)
{
    dispatch_runtime_t output = {};
    output.graph = ProgressiveRenderGraph(_state->core_registry);
    output.vulkan = _vulkan;
    output.registry = &_state->core_registry;
    output.state = _state;
    output.payload = new ProgressivePipelinePayload{};

    BindSparseRegionRuntime(_state, _vulkan,
                            output.graph["FramebufferDispatch"]["SparseRegionSubpass"]);
    BindGUIRuntime(_state, _vulkan,
                   output.graph["FramebufferScreenSpace"]["GUISubpass"]);

    output.DestroyPayload = [](dispatch_runtime_t& _rt)
    {
        _rt.graph.DepthFirstTraversal(
            [&]
            (uint32_t, dstk::Registry::Node node) {
                dispatch_runtime_t* runtime =
                    _rt.registry->ComponentLookup<dispatch_runtime_t>(node);
                if (runtime && runtime->DestroyPayload)
                    runtime->DestroyPayload(*runtime);
            });
    };

    output.ConstantResources = [](dispatch_runtime_t& _rt)
    {
        ProgressivePipelinePayload& payload = *(ProgressivePipelinePayload*)_rt.payload;

        _rt.Context().GenerateStaticRuntime();
        _rt.Context().ResetDynamicRuntime();

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
        CHECKCALL(vkCreateSampler, _rt.vulkan->device,
                  &sampler_create, nullptr, &payload.nearest_sampler);

        _rt.graph.DepthFirstTraversal(
            [&]
            (uint32_t, dstk::Registry::Node node) {
                dispatch_runtime_t* runtime =
                    _rt.registry->ComponentLookup<dispatch_runtime_t>(node);
                if (runtime && runtime->ConstantResources)
                    runtime->ConstantResources(*runtime);
            });
    };

    output.RenderResources = [](dispatch_runtime_t& _rt,
                                vktk::Swapchain const& _swapchain)
    {
        _rt.Context().ClearFramebufferRuntime();
        _rt.Context().GenerateFramebufferRuntime(&_swapchain.format);

        ProgressivePipelinePayload& payload = *(ProgressivePipelinePayload*)_rt.payload;

        {
            _rt.vulkan->DestroyTexture(payload.primary_pass_color);
            payload.primary_pass_color = _rt.vulkan->CreateTexture(
                _swapchain.size.width, _swapchain.size.height, 1,
                VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
                | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
                | VK_IMAGE_USAGE_SAMPLED_BIT
            );

            _rt.vulkan->DestroyTexture(payload.primary_pass_depth);
            payload.primary_pass_depth = _rt.vulkan->CreateTexture(
                _swapchain.size.width, _swapchain.size.height, 1,
                VK_IMAGE_TYPE_2D, VK_FORMAT_D32_SFLOAT,
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
            );

            _rt.vulkan->DestroyTexture(payload.accumulated_color);
            payload.accumulated_color = _rt.vulkan->CreateTexture(
                _swapchain.size.width, _swapchain.size.height, 1,
                VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
                | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
                | VK_IMAGE_USAGE_SAMPLED_BIT
            );
        }

        numtk::vec2u size = { _swapchain.size.width, _swapchain.size.height };
        _rt.Context().BindAttachments(
            _rt.graph["FramebufferDispatch"], size,
            { payload.primary_pass_color.view, payload.primary_pass_depth.view }
        );

        _rt.Context().BindAttachments(
            _rt.graph["FramebufferResolve"], size,
            { payload.accumulated_color.view }
        );

        VkDescriptorImageInfo image_info{};
        image_info.sampler = payload.nearest_sampler;
        image_info.imageView = payload.primary_pass_color.view;
        image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet desc_write = _rt.Context().BindShaderResource(
            _rt.graph["FramebufferResolve"]["ResolvePass"],
            vkrg::layout::BindingPoint{
                .type = vkrg::layout::BindingType_Shader,
                .shader = { 0, 0 }
            },
            0,
            vkrg::bind::ShaderResources{
                .images = { image_info }
            });

        vkUpdateDescriptorSets(_rt.vulkan->device, 1, &desc_write, 0u, nullptr);

        _rt.Context().BindBlitCommand(
            _rt.graph["FramebufferBlit"],
            vkrg::bind::BlitCommand{
                numtk::bounds3u{
                    { 0, 0, 0 }, { _swapchain.size.width, _swapchain.size.height, 1 }
                },
                numtk::bounds3u{
                    { 0, 0, 0 }, { _swapchain.size.width, _swapchain.size.height, 1 }
                },
                VK_IMAGE_ASPECT_COLOR_BIT,
                VK_FILTER_NEAREST
            }
        );

        _rt.Context().BindImage(
            _rt.graph["FramebufferBlit"],
            vkrg::layout::BindingPoint{
                .type = vkrg::layout::BindingType_BlitImage,
                .blit_slot = vkrg::layout::BlitImageSlot_Source
            },
            payload.accumulated_color.image
        );

        _rt.Context().BindSwapchain(_rt.graph["FramebufferScreenSpace"], _swapchain);

        _rt.graph.DepthFirstTraversal(
            [&]
            (uint32_t, dstk::Registry::Node node) {
                dispatch_runtime_t* runtime =
                    _rt.registry->ComponentLookup<dispatch_runtime_t>(node);
                if (runtime && runtime->RenderResources)
                    runtime->RenderResources(*runtime, _swapchain);
            });
    };

    output.Update = [](dispatch_runtime_t& _rt)
    {
        _rt.graph.DepthFirstTraversal(
            [&]
            (uint32_t, dstk::Registry::Node node) {
                dispatch_runtime_t* runtime =
                    _rt.registry->ComponentLookup<dispatch_runtime_t>(node);
                if (runtime && runtime->Update)
                    runtime->Update(*runtime);
            });
    };

    output.Dispatch = [](dispatch_runtime_t& _rt,
                         vktk::CommandBuffer& _command_buffer,
                         vktk::Swapchain const& _swapchain)
    {
        _rt.graph.DepthFirstTraversal(
            [&]
            (uint32_t, dstk::Registry::Node node) {
                dispatch_runtime_t* runtime =
                    _rt.registry->ComponentLookup<dispatch_runtime_t>(node);
                if (runtime && runtime->Dispatch)
                    runtime->Dispatch(*runtime, _command_buffer, _swapchain);
            });

        numtk::bounds2u scissor{ { 0, 0 },
                                 { _swapchain.size.width, _swapchain.size.height } };
        numtk::bounds2f viewport{ { 0.f, 0.f },
                                  { (float)scissor.extent.x(), (float)scissor.extent.y() } };

        _rt.Context().BindFramebufferArea(
            _rt.graph["FramebufferDispatch"],
            { 0, scissor, viewport });

        _rt.Context().BindFramebufferArea(
            _rt.graph["FramebufferResolve"],
            { 0, scissor, viewport });

        _rt.Context().BindImage(
            _rt.graph["FramebufferBlit"],
            vkrg::layout::BindingPoint{
                .type = vkrg::layout::BindingType_BlitImage,
                .blit_slot = vkrg::layout::BlitImageSlot_Sink
            },
            _swapchain.images[_swapchain.index]
        );

        _rt.Context().BindFramebufferArea(
            _rt.graph["FramebufferScreenSpace"], {
                _swapchain.index, scissor, viewport
            }
        );

        _rt.Context().BindGraphicsCommand(
            _rt.graph["FramebufferResolve"]["ResolvePass"],
            0,
            { vkrg::bind::DispatchCommand_Draw, 3, 1 });

        float* push_constants = (float*)_rt.Context().BindPushConstants(
            _rt.graph["FramebufferResolve"]["ResolvePass"],
            {
                .type = vkrg::layout::BindingType_PushConstantRange,
                .push_constant_range = 0
            }, 0);
        *push_constants = 1.f / (float)(_rt.state->progressive_frame_index + 1);

        _rt.Context().Dispatch(_command_buffer);
    };

    output.PostSubmit = [](dispatch_runtime_t& _rt, VkFence _last_submit)
    {
        _rt.graph.DepthFirstTraversal(
            [&]
            (uint32_t, dstk::Registry::Node node) {
                dispatch_runtime_t* runtime =
                    _rt.registry->ComponentLookup<dispatch_runtime_t>(node);
                if (runtime && runtime->PostSubmit)
                    runtime->PostSubmit(*runtime, _last_submit);
            });
    };

    return output;
}


struct GUIPipelinePayload
{
    vktk::Buffer gui_frame_indices;
    vktk::Buffer gui_frames_vertices;
    vktk::Buffer gui_frame_uv;
    vktk::Buffer gui_glyph_indices;
    vktk::Buffer gui_glyph_vertices;

    vktk::Texture gui_font_texture;
    vktk::Texture gui_symbols_texture;
    VkSampler nearest_sampler;
};

void BindGUIRuntime(voxel_state_t* _state, vktk::Context* _vulkan,
                    dstk::Registry::Node _subpass_node)
{
    dispatch_runtime_t runtime = {};
    runtime.vulkan = _vulkan;
    runtime.registry = &_state->core_registry;
    runtime.state = _state;
    runtime.payload = new GUIPipelinePayload{};
    runtime.graph = *runtime.registry->ComponentLookup<vkrg::layout::DispatchGraph>(_subpass_node);

    runtime.DestroyPayload = [](dispatch_runtime_t& _rt)
    {
        GUIPipelinePayload& payload = *(GUIPipelinePayload*)_rt.payload;
        _rt.vulkan->DestroyTexture(payload.gui_font_texture);
        _rt.vulkan->DestroyTexture(payload.gui_symbols_texture);
        vkDestroySampler(_rt.vulkan->device, payload.nearest_sampler, nullptr);
        delete &payload;
    };

    runtime.ConstantResources = [](dispatch_runtime_t& _rt)
    {
        GUIPipelinePayload& payload = *(GUIPipelinePayload*)_rt.payload;

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
        CHECKCALL(vkCreateSampler, _rt.vulkan->device,
                  &sampler_create, nullptr, &payload.nearest_sampler);

        payload.gui_font_texture = _rt.vulkan->CreateTexture(
            96, 60, 1,
            VK_IMAGE_TYPE_2D,
            VK_FORMAT_R8_UINT,
            VK_IMAGE_USAGE_SAMPLED_BIT,
            [](void* buffer) {
                uint64_t const* font_data = guitk::FontData();
                uint8_t* texture_buffer = (uint8_t*)buffer;
                for (uint32_t index = 0; index < 95; ++index)
                {
                    uint32_t glyph_x = index % 16;
                    uint32_t glyph_y = index / 16;
                    uint64_t glyph = font_data[index];
                    for (uint32_t dot_y = 0; dot_y < 10; ++dot_y)
                    {
                        for (uint32_t dot_x = 0; dot_x < 6; ++dot_x)
                        {
                            uint32_t dot_index = (5 - dot_x) + dot_y * 6;
                            bool dot_value = ((glyph >> dot_index) & 1) != 0;

                            uint32_t texel_x = glyph_x * 6 + dot_x;
                            uint32_t texel_y = glyph_y * 10 + dot_y;
                            texture_buffer[texel_x + texel_y*96] = dot_value
                                ? '\255'
                                : '\0';
                        }
                    }
                }
            }
        );

        payload.gui_symbols_texture = _rt.vulkan->CreateTexture(
            8*guitk::SymbolTag_COUNT, 8*2, 1,
            VK_IMAGE_TYPE_2D,
            VK_FORMAT_R8_UINT,
            VK_IMAGE_USAGE_SAMPLED_BIT,
            [](void* buffer) {
                uint64_t const* symbols_data = guitk::SymbolsData();
                uint8_t* texture_buffer = (uint8_t*)buffer;
                for (uint32_t index = 0; index < guitk::SymbolTag_COUNT; ++index)
                {
                    uint64_t glyph = symbols_data[index];
                    for (uint32_t dot_y = 0; dot_y < 8; ++dot_y)
                    {
                        for (uint32_t dot_x = 0; dot_x < 8; ++dot_x)
                        {
                            uint32_t dot_index = dot_x + dot_y*8;
                            bool dot_value = ((glyph >> dot_index) & 1) != 0;

                            uint32_t texel_x = index*8 + dot_x;
                            uint32_t texel_y = dot_y;
                            texture_buffer[texel_x + texel_y*guitk::SymbolTag_COUNT*8] = dot_value
                                ? '\255'
                                : '\0';
                        }
                    }
                }

                for (uint32_t index = 0; index < guitk::SymbolTag_COUNT; ++index)
                {
                    uint64_t glyph = symbols_data[index];
                    for (uint32_t dot_y = 0; dot_y < 8; ++dot_y)
                    {
                        for (uint32_t dot_x = 0; dot_x < 8; ++dot_x)
                        {
                            uint32_t dot_index = dot_x + dot_y*8;
                            bool dot_value = ((glyph >> dot_index) & 1) == 0;

                            uint32_t texel_x = index*8 + dot_x;
                            uint32_t texel_y = 8 + dot_y;
                            texture_buffer[texel_x + texel_y*guitk::SymbolTag_COUNT*8] = dot_value
                                ? '\255'
                                : '\0';
                        }
                    }
                }
            }
        );
    };

    runtime.Update = [](dispatch_runtime_t& _rt)
    {
        guitk::WidgetStates const* gui_record = _rt.state->current_gui_record;

        _rt.Context().UpdateDynamicInvocationCount(
            _rt.graph.data->parent_node,
            gui_record->frames.size());

        _rt.Context().UpdateDynamicInvocationCount(
            _rt.graph["GUIFrames"],
            1);
        _rt.Context().ClearStageBarriers(
            _rt.graph["GUIFrames"]);

        _rt.Context().UpdateDynamicInvocationCount(
            _rt.graph["GUIGlyphs"],
            2);
        _rt.Context().ClearStageBarriers(
            _rt.graph["GUIGlyphs"]);

        if (gui_record->frames.empty())
            return;

        GUIPipelinePayload& payload = *(GUIPipelinePayload*)_rt.payload;

        payload.gui_glyph_indices = _rt.vulkan->CreateBuffer(
            sizeof(uint32_t) * 6 * gui_record->glyphs.size(),
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            [gui_record](void* buffer) {
                uint32_t* indices = (uint32_t*)buffer;

                for (uint32_t frame_index = 0u;
                     frame_index < (uint32_t)gui_record->frames.size();
                     ++frame_index)
                {
                    guitk::FrameData const& frame = gui_record->frames[frame_index];
                    for (uint32_t page_index = 0u;
                         page_index < guitk::GlyphPage_COUNT;
                         ++page_index)
                    {
                        for (uint32_t glyph_index = 0u;
                             glyph_index < frame.glyph_range[page_index].second;
                             ++glyph_index)
                        {
                            uint32_t index_offset = glyph_index*4;
                            uint32_t index_base =
                                (glyph_index
                                 + frame.glyph_range[page_index].first)*6;
                            indices[index_base + 0] = 0 + index_offset;
                            indices[index_base + 1] = 2 + index_offset;
                            indices[index_base + 2] = 1 + index_offset;
                            indices[index_base + 3] = 0 + index_offset;
                            indices[index_base + 4] = 3 + index_offset;
                            indices[index_base + 5] = 2 + index_offset;
                        }
                    }
                }
            },
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        payload.gui_glyph_vertices = _rt.vulkan->CreateBuffer(
            sizeof(numtk::vec2f) * 2 * 4 * gui_record->glyphs.size(),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            [gui_record](void* buffer){
                numtk::vec2f* vertices = (numtk::vec2f*)buffer;
                numtk::vec2f* uvs = vertices + 4 * gui_record->glyphs.size();

                for (uint32_t glyph_index = 0u; glyph_index < (uint32_t)gui_record->glyphs.size(); ++glyph_index)
                {
                    guitk::GlyphInfo const& glyph = gui_record->glyphs[glyph_index];
                    vertices[glyph_index * 4 + 0] = glyph.bounds.min.cast<float>();
                    vertices[glyph_index * 4 + 1] = numtk::vec2f{
                        (float)(glyph.bounds.min.x()+glyph.bounds.extent.x()),
                        (float)glyph.bounds.min.y()
                    };
                    vertices[glyph_index * 4 + 2] = glyph.bounds.max().cast<float>();
                    vertices[glyph_index * 4 + 3] = numtk::vec2f{
                        (float)glyph.bounds.min.x(),
                        (float)(glyph.bounds.min.y()+glyph.bounds.extent.y())
                    };

                    numtk::vec2f const& min = glyph.uv.min;
                    numtk::vec2f max = glyph.uv.max();
                    uvs[glyph_index * 4 + 0] = numtk::vec2f{ min.x(), max.y() };
                    uvs[glyph_index * 4 + 1] = max;
                    uvs[glyph_index * 4 + 2] = numtk::vec2f{ max.x(), min.y() };
                    uvs[glyph_index * 4 + 3] = min;
                }
            },
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        payload.gui_frame_indices = _rt.vulkan->CreateBuffer(
            sizeof(uint32_t) * 6 * 9,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            [](void* buffer) {
                static std::vector<uint32_t> const indices = {
                    0, 2, 1,  1, 2, 3,
                    4, 6, 5,  5, 6, 7,
                    8, 10, 9,  9, 10, 11,
                    12, 14, 13,  13, 14, 15,

                    3, 7, 1,  1, 7, 6,
                    7, 11, 5,  5, 11, 10,
                    11, 15, 9,  9, 15, 14,
                    15, 3, 13,  13, 3, 2,

                    7, 3, 11,  11, 3, 15
                };

                uint32_t* index_data = (uint32_t*)buffer;
                std::copy(indices.begin(), indices.end(), index_data);
            },
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        payload.gui_frames_vertices = _rt.vulkan->CreateBuffer(
            sizeof(numtk::vec2f) * 16 * gui_record->frames.size(),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            [gui_record](void* buffer){
                static float const kBorderSize = 10.f;
                static numtk::vec2f const kBorderOffsets[] = {
                    { kBorderSize, 0 },
                    { 0, kBorderSize },
                    { kBorderSize, kBorderSize },
                    { -kBorderSize, kBorderSize }
                };

                numtk::vec2f* vertices = (numtk::vec2f*)buffer;
                for (uint32_t frame_index = 0u; frame_index < gui_record->frames.size(); ++frame_index)
                {
                    numtk::bounds2u const& frame = gui_record->frames[frame_index].bounds;
                    numtk::vec2f* base = &vertices[frame_index*16];
                    base[0] = frame.min.cast<float>();
                    base[1] = base[0] + kBorderOffsets[0];
                    base[2] = base[0] + kBorderOffsets[1];
                    base[3] = base[0] + kBorderOffsets[2];

                    base[4] = numtk::vec2f{
                        (float)(frame.min.x()+frame.extent.x()),
                        (float)frame.min.y() };
                    base[5] = base[4] + kBorderOffsets[1];
                    base[6] = base[4] - kBorderOffsets[0];
                    base[7] = base[4] + kBorderOffsets[3];

                    base[8] = frame.max().cast<float>();
                    base[9] = base[8] - kBorderOffsets[0];
                    base[10] = base[8] - kBorderOffsets[1];
                    base[11] = base[8] - kBorderOffsets[2];

                    base[12] = numtk::vec2f{
                        (float)frame.min.x(),
                        (float)(frame.min.y()+frame.extent.y()) };
                    base[13] = base[12] - kBorderOffsets[1];
                    base[14] = base[12] + kBorderOffsets[0];
                    base[15] = base[12] - kBorderOffsets[3];
                }
            },
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        payload.gui_frame_uv = _rt.vulkan->CreateBuffer(
            sizeof(numtk::vec2f) * 16,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            [](void* buffer){
                static numtk::vec2f const kBorderOffsets[] = {
                    { 0.5f, 0.f },
                    { 0.f, 0.5f },
                    { 0.5f, 0.5f },
                    { -0.5f, 0.5f }
                };

                numtk::vec2f* vertices = (numtk::vec2f*)buffer;
                vertices[0] = numtk::vec2f{ 0.f, 0.f };
                vertices[1] = vertices[0] + kBorderOffsets[0];
                vertices[2] = vertices[0] + kBorderOffsets[1];
                vertices[3] = vertices[0] + kBorderOffsets[2];

                vertices[4] = numtk::vec2f{ 1.f, 0.f };
                vertices[5] = vertices[4] + kBorderOffsets[1];
                vertices[6] = vertices[4] - kBorderOffsets[0];
                vertices[7] = vertices[4] + kBorderOffsets[3];

                vertices[8] = numtk::vec2f{ 1.f, 1.f };
                vertices[9] = vertices[8] - kBorderOffsets[0];
                vertices[10] = vertices[8] - kBorderOffsets[1];
                vertices[11] = vertices[8] - kBorderOffsets[2];

                vertices[12] = numtk::vec2f{ 0.f, 1.f };
                vertices[13] = vertices[12] - kBorderOffsets[1];
                vertices[14] = vertices[12] + kBorderOffsets[0];
                vertices[15] = vertices[12] - kBorderOffsets[3];
            },
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        for (uint32_t index = 0u; index < (uint32_t)gui_record->frames.size(); ++index)
        {
            guitk::FrameData const& frame = gui_record->frames[index];

            _rt.Context().BindGraphicsCommand(
                _rt.graph["GUIFrames"],
                index,
                {
                    vkrg::bind::DispatchCommand_Draw, 6*9, 1
                });


            _rt.Context().BindVertexInput(
                _rt.graph["GUIFrames"],
                { .type = vkrg::layout::BindingType_VertexInput,
                  .vertex_input_index = 0 },
                index,
                { payload.gui_frames_vertices, index * sizeof(numtk::vec2f) * 16 }
            );

            _rt.Context().BindVertexInput(
                _rt.graph["GUIFrames"],
                { .type = vkrg::layout::BindingType_VertexInput,
                  .vertex_input_index = 1 },
                index,
                { payload.gui_frame_uv, 0 }
            );

            _rt.Context().BindIndexInput(
                _rt.graph["GUIFrames"],
                index,
                { payload.gui_frame_indices, 0 }
            );

            dstk::Registry::Node glyphs_node =
                _rt.graph["GUIGlyphs"];

            if (frame.glyph_range[guitk::GlyphPage_Font].second)
            {
                _rt.Context().BindGraphicsCommand(
                    glyphs_node,
                    { 0, index },
                    {
                        vkrg::bind::DispatchCommand_Draw,
                        (uint32_t)frame.glyph_range[guitk::GlyphPage_Font].second*6, 1
                    });

                uint32_t glyph_vertex_offset =
                    frame.glyph_range[guitk::GlyphPage_Font].first*sizeof(numtk::vec2f)*4;
                _rt.Context().BindVertexInput(
                    glyphs_node,
                    { .type = vkrg::layout::BindingType_VertexInput,
                      .vertex_input_index = 0 },
                    { 0, index },
                    { payload.gui_glyph_vertices,
                      glyph_vertex_offset }
                );

                _rt.Context().BindVertexInput(
                    glyphs_node,
                    { .type = vkrg::layout::BindingType_VertexInput,
                      .vertex_input_index = 1 },
                    { 0, index },
                    { payload.gui_glyph_vertices,
                      (uint32_t)gui_record->glyphs.size()*sizeof(numtk::vec2f)*4
                      + glyph_vertex_offset }
                );

                _rt.Context().BindIndexInput(
                    glyphs_node,
                    { 0, index },
                    { payload.gui_glyph_indices,
                      (uint32_t)frame.glyph_range[guitk::GlyphPage_Font].first
                      *sizeof(uint32_t)*6 }
                );

                VkDescriptorImageInfo image_info{
                    payload.nearest_sampler,
                    payload.gui_font_texture.view,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                };

                VkWriteDescriptorSet binding =
                    _rt.Context().BindShaderResource(
                        glyphs_node,
                        vkrg::layout::BindingPoint{
                            .type = vkrg::layout::BindingType_Shader,
                            .shader = { 0, 0 }
                        },
                        { 0, index },
                        vkrg::bind::ShaderResources{
                            .images = { image_info }
                        });

                vkUpdateDescriptorSets(_rt.vulkan->device, 1, &binding, 0u, nullptr);
            }

            if (frame.glyph_range[guitk::GlyphPage_Symbols].second)
            {
                _rt.Context().BindGraphicsCommand(
                    glyphs_node,
                    { 1, index },
                    {
                        vkrg::bind::DispatchCommand_Draw,
                        (uint32_t)frame.glyph_range[guitk::GlyphPage_Symbols].second*6, 1
                    });

                uint32_t glyph_vertex_offset =
                    frame.glyph_range[guitk::GlyphPage_Symbols].first*sizeof(numtk::vec2f)*4;
                _rt.Context().BindVertexInput(
                    glyphs_node,
                    { .type = vkrg::layout::BindingType_VertexInput,
                      .vertex_input_index = 0 },
                    { 1, index },
                    { payload.gui_glyph_vertices,
                      glyph_vertex_offset }
                );

                _rt.Context().BindVertexInput(
                    glyphs_node,
                    { .type = vkrg::layout::BindingType_VertexInput,
                      .vertex_input_index = 1 },
                    { 1, index },
                    { payload.gui_glyph_vertices,
                      (uint32_t)gui_record->glyphs.size()*sizeof(numtk::vec2f)*4
                      + glyph_vertex_offset }
                );

                _rt.Context().BindIndexInput(
                    glyphs_node,
                    { 1, index },
                    { payload.gui_glyph_indices,
                      (uint32_t)frame.glyph_range[guitk::GlyphPage_Symbols].first
                      *sizeof(uint32_t)*6 }
                );

                VkDescriptorImageInfo image_info{
                    payload.nearest_sampler,
                    payload.gui_symbols_texture.view,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                };

                VkWriteDescriptorSet binding =
                    _rt.Context().BindShaderResource(
                        glyphs_node,
                        vkrg::layout::BindingPoint{
                            .type = vkrg::layout::BindingType_Shader,
                            .shader = { 0, 0 }
                        },
                        { 1, index },
                        vkrg::bind::ShaderResources{
                            .images = { image_info }
                        });

                vkUpdateDescriptorSets(_rt.vulkan->device, 1, &binding, 0u, nullptr);
            }
        }

        _rt.Context().BindBarrier(
            _rt.graph["GUIFrames"], {
                .type = vkrg::bind::BufferResource,
                .stage = VK_PIPELINE_STAGE_TRANSFER_BIT,
                .access = VK_ACCESS_TRANSFER_WRITE_BIT,
                .buffer = payload.gui_frames_vertices
            });

        _rt.Context().BindBarrier(
            _rt.graph["GUIFrames"], {
                .type = vkrg::bind::BufferResource,
                .stage = VK_PIPELINE_STAGE_TRANSFER_BIT,
                .access = VK_ACCESS_TRANSFER_WRITE_BIT,
                .buffer = payload.gui_frame_indices
            });

        _rt.Context().BindBarrier(
            _rt.graph["GUIFrames"], {
                .type = vkrg::bind::BufferResource,
                .stage = VK_PIPELINE_STAGE_TRANSFER_BIT,
                .access = VK_ACCESS_TRANSFER_WRITE_BIT,
                .buffer = payload.gui_frame_uv
            });

        _rt.Context().BindBarrier(
            _rt.graph["GUIGlyphs"], {
                .type = vkrg::bind::BufferResource,
                .stage = VK_PIPELINE_STAGE_TRANSFER_BIT,
                .access = VK_ACCESS_TRANSFER_WRITE_BIT,
                .buffer = payload.gui_glyph_vertices
            });

        _rt.Context().BindBarrier(
            _rt.graph["GUIGlyphs"], {
                .type = vkrg::bind::BufferResource,
                .stage = VK_PIPELINE_STAGE_TRANSFER_BIT,
                .access = VK_ACCESS_TRANSFER_WRITE_BIT,
                .buffer = payload.gui_glyph_indices
            });
    };

    runtime.Dispatch = [](dispatch_runtime_t& _rt,
                          vktk::CommandBuffer& _command_buffer,
                          vktk::Swapchain const& _swapchain)
    {
        numtk::vec2f swapchain_size{ (float)_swapchain.size.width, (float)_swapchain.size.height };

        for (uint32_t index = 0u;
             index < (uint32_t)_rt.state->current_gui_record->frames.size();
             ++index)
        {
            numtk::vec2f* frame_constants =
                (numtk::vec2f*)_rt.Context().BindPushConstants(
                    _rt.graph["GUIFrames"],
                    {
                        .type = vkrg::layout::BindingType_PushConstantRange,
                        .push_constant_range = 0
                    }, index);
            frame_constants[0] = swapchain_size;

            numtk::vec2f* font_constants =
                (numtk::vec2f*)_rt.Context().BindPushConstants(
                    _rt.graph["GUIGlyphs"],
                    {
                        .type = vkrg::layout::BindingType_PushConstantRange,
                        .push_constant_range = 0
                    }, { 0, index });
            font_constants[0] = swapchain_size;

            numtk::vec2f* symbols_constants =
                (numtk::vec2f*)_rt.Context().BindPushConstants(
                    _rt.graph["GUIGlyphs"],
                    {
                        .type = vkrg::layout::BindingType_PushConstantRange,
                        .push_constant_range = 0
                    }, { 1, index });
            symbols_constants[0] = swapchain_size;
        }
    };

    runtime.PostSubmit = [](dispatch_runtime_t& _rt, VkFence _last_submit)
    {
        GUIPipelinePayload& payload = *(GUIPipelinePayload*)_rt.payload;
        _rt.vulkan->ScheduleRoutine(
            _last_submit,
            [index_buffer = payload.gui_frame_indices
             , vertex_buffer = payload.gui_frames_vertices
             , uv_buffer = payload.gui_frame_uv
             , glyph_indices = payload.gui_glyph_indices
             , glyph_vertices = payload.gui_glyph_vertices](vktk::Context& _vulkan) {
                _vulkan.DestroyBuffer(index_buffer);
                _vulkan.DestroyBuffer(vertex_buffer);
                _vulkan.DestroyBuffer(uv_buffer);
                _vulkan.DestroyBuffer(glyph_indices);
                _vulkan.DestroyBuffer(glyph_vertices);
            });
    };

    runtime.registry->BindComponent(_subpass_node, runtime);
}

void BindSparseRegionRuntime(voxel_state_t* _state, vktk::Context* _vulkan,
                             dstk::Registry::Node _subpass_node)
{
    dispatch_runtime_t runtime = {};
    runtime.vulkan = _vulkan;
    runtime.registry = &_state->core_registry;
    runtime.state = _state;
    runtime.payload = new SparsePipelinePayload{};
    runtime.graph = *runtime.registry->ComponentLookup<vkrg::layout::DispatchGraph>(_subpass_node);

    runtime.DestroyPayload = [](dispatch_runtime_t& _rt)
    {
        SparsePipelinePayload& payload = *(SparsePipelinePayload*)_rt.payload;
        vkDestroySampler(_rt.vulkan->device, payload.nearest_sampler, nullptr);
        _rt.vulkan->DestroyBuffer(payload.matrix_buffer);
        _rt.vulkan->DestroyBuffer(payload.staging_buffer);
        delete &payload;
    };

    runtime.ConstantResources = [](dispatch_runtime_t& _rt)
    {
        SparsePipelinePayload& payload = *(SparsePipelinePayload*)_rt.payload;

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
        CHECKCALL(vkCreateSampler, _rt.vulkan->device,
                  &sampler_create, nullptr, &payload.nearest_sampler);

        payload.staging_buffer = _rt.vulkan->CreateBuffer(
            sizeof(MatrixBlock),
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
            | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        payload.matrix_buffer = _rt.vulkan->CreateBuffer(
            sizeof(MatrixBlock),
            VK_BUFFER_USAGE_TRANSFER_DST_BIT
            | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);

        MatrixBlock* matrix = (MatrixBlock*)payload.staging_buffer.alloc.map;
        matrix->projection = numtk::mat4f::Constant(0.f);
        matrix->view_model = numtk::mat4f::Constant(0.f);
        matrix->view = numtk::mat4f::Constant(0.f);
        matrix->screen_size = numtk::vec2<int32_t>::Constant(1);

        {
            vktk::CommandBuffer command_buffer =
                _rt.vulkan->CreateCommandBuffer(_rt.vulkan->present_queue);
            command_buffer.Begin();

            command_buffer.BufferTransitions(
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                {
                    { payload.matrix_buffer,
                      VK_ACCESS_NONE,
                      VK_ACCESS_TRANSFER_WRITE_BIT },
                });

            command_buffer.BufferCopy(payload.matrix_buffer, 0, payload.staging_buffer, 0,
                                      sizeof(MatrixBlock));

            command_buffer.BufferTransitions(
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
                {
                    { payload.matrix_buffer,
                      VK_ACCESS_TRANSFER_WRITE_BIT,
                      VK_ACCESS_UNIFORM_READ_BIT }
                });

            command_buffer.End();
            command_buffer.Submit();
        }

        SparseRegionUpdateBindings(_rt);
    };

    runtime.RenderResources = [](dispatch_runtime_t& _rt,
                                 vktk::Swapchain const& _swapchain)
    {};

    runtime.Update = [](dispatch_runtime_t& _rt)
    {};

    runtime.Dispatch = [](dispatch_runtime_t& _rt,
                          vktk::CommandBuffer& _command_buffer,
                          vktk::Swapchain const& _swapchain)
    {
        SparsePipelinePayload& payload = *(SparsePipelinePayload*)_rt.payload;

        numtk::vec2f swapchain_size{ (float)_swapchain.size.width, (float)_swapchain.size.height };
        float how = (float)_swapchain.size.height / (float)_swapchain.size.width;

        {
            MatrixBlock* matrix = (MatrixBlock*)payload.staging_buffer.alloc.map;
            matrix->view = _rt.state->camera.Matrix();
            matrix->projection =
                numtk::mat4f::Perspective_VulkanNDC(1.0f, 1000000.f, 90.f*numtk::kDegrees, how);
            matrix->view_model =
                matrix->view
                * numtk::mat4f{
                131072.f, 0.f, 0.f, 0.f,
                0.f, 131072.f, 0.f, 0.f,
                0.f, 0.f, 131072.f, 0.f,
                0.f, 0.f, 0.f, 1.f };
            matrix->screen_offset = numtk::vec2f{ 0.f, 0.f };
            matrix->screen_size = swapchain_size.cast<int32_t>();

            _command_buffer.BufferTransitions(
                VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                {
                    { payload.matrix_buffer,
                      VK_ACCESS_UNIFORM_READ_BIT,
                      VK_ACCESS_TRANSFER_WRITE_BIT }
                });

            _command_buffer.BufferCopy(payload.matrix_buffer, 0,
                                       payload.staging_buffer, 0,
                                       sizeof(MatrixBlock));

            _command_buffer.BufferTransitions(
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
                {
                    { payload.matrix_buffer,
                      VK_ACCESS_TRANSFER_WRITE_BIT,
                      VK_ACCESS_UNIFORM_READ_BIT }
                });
        }

        numtk::vec3f sun_dir = numtk::vec3f{
            std::cos(_rt.state->sun_spherical[1]) * std::cos(_rt.state->sun_spherical[0]),
            std::sin(_rt.state->sun_spherical[1]),
            std::cos(_rt.state->sun_spherical[1]) * std::sin(_rt.state->sun_spherical[0])
        }.normalise();

        dstk::Registry::Node sparse_region_node =
            _rt.graph["SparseRegion"];

        vkrg::bind::GraphicsCommand command = {};
        command.command = vkrg::bind::DispatchCommand_Draw;
        command.vertex_count = 36;
        command.instance_count = 1;
        _rt.Context().BindGraphicsCommand(sparse_region_node, 0, command);

        SparseRegionPushConstants* push_constants =
            (SparseRegionPushConstants*)_rt.Context().BindPushConstants(
                sparse_region_node,
                {
                    .type = vkrg::layout::BindingType_PushConstantRange,
                    .push_constant_range = 0
                }, 0);
        push_constants->light_dir = sun_dir;
        push_constants->display_mode = _rt.state->enable_indirect_light ? 1.f : 0.f;
        push_constants->node_size = (float)_rt.state->region_package.node_size;
        push_constants->stack_depth = std::max(_rt.state->region_package.display_depth-1, 0);
        push_constants->sample_offset = _rt.state->progressive_frame_index;
        push_constants->leaves_begin = _rt.state->region_package.leaves_begin;
    };

    runtime.PostSubmit = [](dispatch_runtime_t& _rt, VkFence _last_submit)
    {};

    runtime.registry->BindComponent(_subpass_node, runtime);
}

std::unordered_map<std::string, numtk::vec3<uint8_t>> const& BlockColorMap()
{
#include "mcblocks.hh"
    return kBlockColors;
}

}
