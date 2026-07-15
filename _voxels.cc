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
#include <chrono>
#include <future>
#include <memory>
#include <unordered_map>

//#define SYNC_LOAD
#define VOXEL_NODE_SIZE 16u

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
    numtk::vec2f forward_spherical = { 0.710f, -0.260f };
    numtk::vec3f position = { 37.025f, -36.243f, -18.561f };

    float velocity = 5.f;

    void Update(iotk::input_t const* _input,
                iotk::input_t const* _past_input);
    numtk::mat4f Matrix() const;
    bool updated;
};

struct VoxelRegionPackage
{
    numtk::vec3f area_scale;
    int32_t stack_depth;
    int32_t display_depth;
    uint32_t leaves_begin;
    std::vector<vktk::Texture> masks{};
    std::vector<vktk::Texture> offsets{};
    std::vector<vktk::Texture> payloads{};
};

VoxelRegionPackage UploadVoxelRegion(vktk::Context* vulkan,
                                     voxtk::VoxelField const& region);

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

void SparseRegionUpdateBindings(dispatch_runtime_t& _rt, VoxelRegionPackage const& _region_package);

struct VoxelRegionFuture
{
    voxtk::VoxelField region;
};

struct voxel_state_t
{
    guitk::Context gui_context;
    guitk::WidgetStates const* current_gui_record;

    float time = 0.f;

    float framerate_average = 0.f;
    std::string framerate_label;
    bool enable_indirect_light = false;
    float sun_power = 0.f;

    //TurntableCamera camera;
    FlightCamera camera;

    numtk::vec2f sun_spherical = { -2.f, 0.25f };

    uint32_t frame_index = 0;

    uint32_t progressive_frame_index = 0;

    dstk::Registry core_registry;
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
    voxtk::VoxelField initial_region;
    voxtk::VoxelField voxel_region{};

    mutable VoxelRegionPackage region_package;
    mutable std::future<VoxelRegionFuture> region_future = {};
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
    numtk::vec3f region_scale;
    float node_size;
    int32_t stack_depth;
    uint32_t sample_offset;
    uint32_t leaves_begin;
    float sun_power;
};

struct SkyboxPushConstants
{
    numtk::mat4f projection;
    numtk::mat4f view;
    numtk::vec4f position;
    numtk::vec3f light_dir;
};

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

void DecodeChunk(voxtk::VoxelField& region,
                 mctk::MCAChunkData const& chunk_data,
                 numtk::vec2i const& chunk_xz)
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
                    numtk::vec3i section_begin =
                        numtk::vec3i{ chunk_xz.x(), section_y, chunk_xz.y() } * 16;
                    numtk::vec3i section_end =
                        section_begin + numtk::vec3i::Constant(16);

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
                        if (palette_data[0] != "air")
                            region.SetVolume(numtk::bounds3i{ section_begin, section_end }, true);
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

                        numtk::vec3i point = section_begin + numtk::vec3u{ x, y, z }.cast<int32_t>();
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
#if 0
                        else if (palette_data[block_id] != "air"
                                 && palette_data[block_id] != "glow_lichen"
                                 && palette_data[block_id] != "water")
                            std::cout << palette_data[block_id]  << std::endl;
#endif

                        data_offset += bit_count;
                        ++block_index;
                    }
                }
            }
        }
    }
}

voxtk::VoxelField DeserialiseRegion(std::string const& file_path)
{
    systk::Timer scope_time{ "DeserialiseRegion",
        [](std::string const& tag, float time) {
            std::cout << "[" << tag << "] " << time << " s" << std::endl;
        }
    };

    std::vector<uint8_t> region_file = systk::LoadFile(file_path.c_str());
    mctk::MCAFileData mca_data = mctk::MCADecode(region_file.data(), region_file.size());

    voxtk::VoxelField output{};
    output.DeclareLayer(0, numtk::vec3<uint8_t>{ 0, 0, 0 });

#if 0
    for (uint32_t index = 0; index < 1024; ++index)
    {
        uint32_t chunk_x = index % 32;
        uint32_t chunk_z = index / 32;
        DecodeChunk(output, mca_data.chunks[index],
                    numtk::vec2u{ chunk_x, chunk_z }.cast<int32_t>());
    }

#else

#define WORKER_COUNT 16
    std::vector<std::thread> workers{};
    std::vector<voxtk::VoxelField> workers_outputs{};
    workers_outputs.reserve(WORKER_COUNT);
    for (uint32_t worker_index = 0; worker_index < WORKER_COUNT; ++worker_index)
    {
        workers_outputs.emplace_back();
        workers.emplace_back([&, worker_index](){
            systk::Timer thread_time{ "WorkerTime",
                [](std::string const& tag, float time) {
                    std::cout << "[" << tag << "] " << time << " s" << std::endl;
                }
            };

            voxtk::VoxelField& thread_output = workers_outputs[worker_index];
            thread_output.DeclareLayer(0, numtk::vec3<uint8_t>{ 0, 0, 0 });

            uint32_t job_size = 1024 / WORKER_COUNT;
            for (uint32_t local_index = 0; local_index < job_size; ++local_index)
            {
                uint32_t global_index = local_index + worker_index*job_size;
                uint32_t chunk_x = global_index % 32;
                uint32_t chunk_z = global_index / 32;
                DecodeChunk(thread_output,
                            mca_data.chunks[global_index],
                            numtk::vec2u{ chunk_x, chunk_z }.cast<int32_t>());
            }
        });
    }

    for (uint32_t worker_index = 0u; worker_index < WORKER_COUNT; ++worker_index)
    {
        std::thread& worker = workers[worker_index];
        if (worker.joinable())
            worker.join();
        output.SetField({ 0, 0, 0 }, workers_outputs[worker_index]);
    }

#endif

#if 0
    voxtk::VoxelField copy{};
    copy.DeclareLayer(0, numtk::vec3<uint8_t>{ 0, 0, 0 });
    copy.SetField({ 0, 0, 0 }, output);
    output.SetField({ 512, 0, 0 }, output);
    for (int32_t index = 1; index < 16; ++index)
    {
        output.SetField({ 512 * (index%4), 0, 512 * (index/4) }, copy);
        if (((index%4) & (index/4)) & 1)
            output.SetField({ 0, 512 * 3, 0 }, copy);
    }
#endif
    return output;
}

VoxelRegionPackage UploadVoxelRegion(vktk::Context* vulkan,
                                     voxtk::VoxelField const& region)
{
    systk::Timer scope_time{ "UploadVoxelRegion",
        [](std::string const& tag, float time) {
            std::cout << "[" << tag << "] " << time << " s" << std::endl;
        }
    };

    constexpr uint32_t node_size = VOXEL_NODE_SIZE;
    numtk::vec3u area_size = region.bounds.extent().cast<uint32_t>();

    VoxelRegionPackage package = {};

    uint32_t log_node_size = (uint32_t)numtk::BitTrailingZeroCount(VOXEL_NODE_SIZE*4);
    numtk::vec3u log_area_size{
        (uint32_t)(64 - numtk::BitLeadingZeroCount(area_size.x - 1)),
        (uint32_t)(64 - numtk::BitLeadingZeroCount(area_size.y - 1)),
        (uint32_t)(64 - numtk::BitLeadingZeroCount(area_size.z - 1))
    };
    uint32_t max_log_area_size = std::max(log_area_size.x,
                                          std::max(log_area_size.y, log_area_size.z));
    uint32_t max_area_size = (std::max(area_size.x, std::max(area_size.y, area_size.z)) + 3) / 4;

    uint32_t depth = (uint32_t)((max_log_area_size + (log_node_size-1)) / log_node_size);

    package.stack_depth = depth*2;
    package.display_depth = package.stack_depth;
    package.area_scale = numtk::vec3f::Constant(1.f) / numtk::vec3f{
        (float)(1u << ((log_node_size * depth) - log_area_size.x)),
        (float)(1u << ((log_node_size * depth) - log_area_size.y)),
        (float)(1u << ((log_node_size * depth) - log_area_size.z))
    };

    assert(package.stack_depth < 5);

    uint32_t kScaleRatio = max_area_size / node_size;
    uint32_t kLogScale = (uint32_t)std::popcount((node_size*4) - 1u);
    uint32_t kNodeMortonMask = (node_size - 1u);
    uint32_t kNodeMortonShift = (uint32_t)std::popcount(kNodeMortonMask);
    uint32_t kChunkMortonMask = 0x3;
    uint32_t kChunkMortonShift = 2;
    uint32_t kLevelMortonShift = (kNodeMortonShift+kChunkMortonShift)*3;

    std::vector<voxtk::VoxelField> lods = {};

    voxtk::VoxelField const* src = &region;
    for (uint32_t lod_size = kScaleRatio, src_index = 0u;
         lod_size > 1;
         (lod_size /= node_size*4, ++src_index))
    {
        lods.push_back(voxtk::VoxelField{});

        if (src_index)
            src = &lods[src_index-1];
        voxtk::VoxelField& dst = lods.back();

        numtk::vec3i dst_min{ 0, 0, 0 };
        numtk::vec3i dst_extent = numtk::vec3i::Constant((int32_t)lod_size);
        for (int32_t dstz = 0; dstz < dst_extent.z; ++dstz)
        {
            for (int32_t dsty = 0; dsty < dst_extent.y; ++dsty)
            {
                for (int32_t dstx = 0; dstx < dst_extent.x; ++dstx)
                {
                    numtk::vec3i dstbase =
                        dst_min + numtk::vec3i{ dstx, dsty, dstz };

                    bool bit = []
                        (uint32_t node_size,
                         voxtk::VoxelField const& src,
                         numtk::vec3i const& dstbase) {
                        for (int32_t srcz = 0; srcz < node_size; ++srcz)
                        {
                            for (int32_t srcy = 0; srcy < node_size; ++srcy)
                            {
                                for (int32_t srcx = 0; srcx < node_size; ++srcx)
                                {
                                    numtk::vec3i srcbase = dstbase*(int32_t)node_size*4
                                        + numtk::vec3i{ srcx, srcy, srcz }*4;
                                    uint64_t srcbits = src.TestKernel(srcbase);
                                    if (srcbits != 0)
                                        return true;
                                }
                            }
                        }
                        return false;
                    }(node_size, *src, dstbase);

                    ((voxtk::VoxelField&)dst).Set(dstbase, bit);
                }
            }
        }
    }

    lods.push_back(voxtk::VoxelField{});
    lods.back().Set({ 0, 0, 0 }, true);

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
        voxtk::VoxelField const& lod = lods[lod_count-1 - level_index];
        numtk::vec3u lod_size = lod.bounds.extent().cast<uint32_t>();
        numtk::vec3u nextlod_size = numtk::max(lod_size / (node_size * 4),
                                               numtk::vec3u::Constant(1));
        numtk::vec3u nextlod_chunkcount = numtk::max(nextlod_size / 4,
                                                    numtk::vec3u::Constant(1));
        numtk::vec3u nextlod_chunksize = (level_index == 0)
            ? numtk::vec3u{ 4, 4, 4 }
            : numtk::min(lods[lod_count-level_index].bounds.extent().cast<uint32_t>(),
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

            if (level_index != 0u
                && !lods[lod_count - level_index].Test(nextlod_base.cast<int32_t>()))
                continue;

            numtk::vec3u const lod_base = nextlod_base * node_size*4;

            numtk::vec3u const valid_range{
                std::min(node_size, std::max((lod_size.x-lod_base.x)/4u, 1u)),
                std::min(node_size, std::max((lod_size.y-lod_base.y)/4u, 1u)),
                std::min(node_size, std::max((lod_size.z-lod_base.z)/4u, 1u))
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
                                    numtk::vec3i base = (numtk::vec3u{
                                        xindex * 4,
                                        yindex * 4,
                                        zindex * 4
                                    } + lod_base).cast<int32_t>();

                                    uint64_t bits = lod.TestKernel(base);
                                    output[xindex + yindex*node_size + zindex*node_size*node_size] = bits;

                                    level_offsets.push_back(current_offset);
                                    current_offset += std::popcount(bits);
                                }

                                if (valid_range.x != node_size)
                                {
                                    level_offsets.resize(level_offsets.size()
                                                         + (node_size-valid_range.x),
                                                         level_offsets.back());

                                    std::memset(output +
                                                valid_range.x +
                                                yindex*node_size +
                                                zindex*node_size*node_size,
                                                0,
                                                sizeof(uint64_t) * (node_size-valid_range.x));
                                }
                            }

                            if (valid_range.y != node_size)
                            {
                                level_offsets.resize(level_offsets.size()
                                                     + (node_size-valid_range.y)*node_size,
                                                     level_offsets.back());

                                std::memset(output +
                                            valid_range.y*node_size +
                                            zindex*node_size*node_size,
                                            0,
                                            sizeof(uint64_t) * (node_size-valid_range.y)*node_size);
                            }
                        }

                        if (valid_range.z != node_size)
                        {
                            level_offsets.resize(level_offsets.size()
                                                 + (node_size-valid_range.z)*node_size*node_size,
                                                 level_offsets.back());

                            std::memset(output +
                                        valid_range.z*node_size*node_size,
                                        0,
                                        sizeof(uint64_t) * (node_size-valid_range.z)*node_size*node_size);
                        }
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

        if (!lods[0].Test(lod1_base.cast<int32_t>()))
            continue;

        numtk::vec3u const lod0_base = lod1_base * node_size*4u;

        std::vector<vktk::ImageTransition> pre_transitions = {};
        std::vector<vktk::ImageTransition> post_transitions = {};

        vktk::Texture mask = vulkan->CreateTexture(
            node_size, node_size, node_size,
            VK_IMAGE_TYPE_3D, VK_FORMAT_R32G32_UINT,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
        package.masks.push_back(mask);

        pre_transitions.push_back(vktk::ImageTransition{
            mask.image,
            VK_ACCESS_NONE, VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_ASPECT_COLOR_BIT,
        });

        post_transitions.push_back(vktk::ImageTransition{
            mask.image,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_ASPECT_COLOR_BIT,
        });

        vktk::Texture payload = {};
        if (region.HasLayer(0))
        {
            payload = vulkan->CreateTexture(
                node_size * 4u, node_size * 4u, node_size * 4u,
                VK_IMAGE_TYPE_3D, VK_FORMAT_R8G8B8A8_UNORM,
                VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
            package.payloads.push_back(payload);

            pre_transitions.push_back(vktk::ImageTransition{
                payload.image,
                VK_ACCESS_NONE, VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_ASPECT_COLOR_BIT
            });

            post_transitions.push_back(vktk::ImageTransition{
                payload.image,
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_IMAGE_ASPECT_COLOR_BIT
            });
        }

        upload_commands.ImageTransitions(VK_PIPELINE_STAGE_HOST_BIT,
                                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                                         pre_transitions);

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
                            numtk::vec3 base = numtk::vec3u{
                                xindex * 4,
                                yindex * 4,
                                zindex * 4
                            } + lod0_base;

                            uint64_t bits = region.TestKernel(base.cast<int32_t>());
                            output[xindex + yindex*node_size + zindex*node_size*node_size] = bits;
                        }
                    }
                }
            }
        );

        if (payload.image != VK_NULL_HANDLE)
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
                                        (lod0_base + numtk::vec3u{ xindex, yindex, zindex })
                                        .cast<int32_t>(),
                                        0);

                                output[xindex
                                       + yindex*extent
                                       + zindex*extent*extent] = numtk::vec4<uint8_t>{
                                    data.x, data.y, data.z, 0
                                };
                            }
                        }
                    }
                }
            );

        upload_commands.ImageTransitions(VK_PIPELINE_STAGE_TRANSFER_BIT,
                                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                         post_transitions);

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
#if 0
    {
        voxtk::VoxelField src{ numtk::vec3u{ 16, 16, 16 } };
        src.DeclareLayer(0, numtk::vec3<uint8_t>{ 255, 255, 255 });
        for (uint32_t z = 0; z < 16; ++z)
            for (uint32_t y = 0; y < 16; ++y)
                for (uint32_t x = 0; x < 16; ++x)
                    src.Set(numtk::vec3u{ x, y, z }, !!(((x&1) + (y&1) + (z&1)) & 1));

        voxtk::VoxelField mask{ numtk::vec3u{ 2, 2, 2 } };
        mask.Set(numtk::bounds3u{{ 0, 0, 0 }, { 2, 2, 2 }}, true);

        voxtk::VoxelField src_shifted = src.Shift(numtk::vec3i{ 0, -7, 0 });
        _state->voxel_region = src.BitwiseAnd(numtk::vec3i{ 0, 7, 0 }, mask);
        //_state->voxel_region = src_shifted;
        //_state->voxel_region = src;

        area_size = 4u;
        node_size = 4u;
        int i = 0;
    }
#endif

    _state->sparse_runtime = SparseRenderingPipeline(_state, _vulkan);
    _state->progressive_runtime = ProgressiveRenderingPipeline(_state, _vulkan);

#if 1

    _state->region_future = std::async(
        std::launch::async,
        []() -> VoxelRegionFuture {
            VoxelRegionFuture output = {};
            output.region = DeserialiseRegion("../r.-1.6.mca");
            return std::move(output);
        }
    );

#else

    _state->region_future = std::async(
        std::launch::async,
        []() -> VoxelRegionFuture {
            VoxelRegionFuture output = {};

            {
                systk::Timer scope_time{ "COPC import",
                    [](std::string const& tag, float time) {
                        std::cout << "[" << tag << "] " << time << " s" << std::endl;
                    }
                };

                output.region =
                    voxtk::importer::ImportCOPC(
                        "../LHD_FXX_0697_6520_PTS_LAMB93_IGN69.copc.laz",
                        //"../LHD_FXX_0697_6520_PTS_C_LAMB93_IGN69.copc.laz",
                        4096);
            }

            return std::move(output);
        }
    );
#endif

#ifdef SYNC_LOAD
    _state->region_future.wait();
    {
        VoxelRegionFuture voxel_region = _state->region_future.get();
        _state->region_package = UploadVoxelRegion(_vulkan,
                                                   voxel_region.region);
    }
#endif


#if 0
    _state->initial_region = _state->voxel_region;
    _state->region_package = UploadVoxelRegion(_vulkan, _state->voxel_region);
#endif

#if 0
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
        guitk::BeginFrame(_state->gui_context, "MainFrame");

        guitk::Label(_state->gui_context, _state->framerate_label.c_str());

        {
            std::string posstr = "position : "
                + std::to_string(_state->camera.position.x) + " "
                + std::to_string(_state->camera.position.y) + " "
                + std::to_string(_state->camera.position.z);
            guitk::Label(_state->gui_context, posstr.c_str());
        }

        {
            std::string rotstr = "forward : "
                + std::to_string(_state->camera.forward_spherical.x()) + " "
                + std::to_string(_state->camera.forward_spherical.y());
            guitk::Label(_state->gui_context, rotstr.c_str());
        }

        guitk::Checkbox(_state->gui_context, &_state->enable_indirect_light, "Indirect Lighting");

        if (guitk::Slider(_state->gui_context, &_state->sun_power, "Sun Power", 0.f, 1.f))
            reset_progressive_counter = true;

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
        _state->enable_indirect_light = !_state->enable_indirect_light;

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

#if 0
    if (stencil_delta != numtk::vec3i::Constant(0))
    {
        _state->stencil_location += stencil_delta;

#if 1
        _state->voxel_region = _state->initial_region.Shift(_state->stencil_location);
        _state->region_package = UploadVoxelRegion(_vulkan, _state->voxel_region);

        SparseRegionUpdateBindings(_state->sparse_runtime);
#else

        _state->stencil_location = numtk::min(numtk::max(_state->stencil_location,
                                                         numtk::vec3i::Constant(-16)),
                                              numtk::vec3i::Constant(16));

        {
            voxtk::VoxelField src{ numtk::vec3u{ 16, 16, 16 } };
            for (uint32_t z = 0; z < 16; ++z)
                for (uint32_t y = 0; y < 16; ++y)
                    for (uint32_t x = 0; x < 16; ++x)
                        src.Set(numtk::vec3u{ x, y, z }, !!(((x&1) + (y&1) + (z&1)) & 1));

            voxtk::VoxelField mask{ numtk::vec3u{ 3, 3, 3 } };
            //mask.Set({ 0, 0, 0 }, { 3, 3, 3 }, uint8_t(0));
            mask.Set(numtk::bounds3u{{ 0, 0, 0 }, { 3, 3, 3 }}, true);

            //_state->voxel_region = src.BitwiseAnd(mask, _state->stencil_location);
            _state->voxel_region = src.BitwiseAnd(_state->stencil_location.cast<int32_t>(), mask);
            //_state->voxel_region = src.Shift(_state->stencil_location);

            uint32_t area_size = 4u;
            uint32_t node_size = 4u;
            _state->region_package = UploadVoxelRegion(_vulkan, _state->voxel_region);

            SparseRegionUpdateBindings(_state->sparse_runtime);
        }
#endif
    }
#endif

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

    vktk::CommandBuffer command_buffer = _vulkan->CreateCommandBuffer(_vulkan->present_queue);
    command_buffer.Begin();

    //_state->sparse_runtime.Dispatch(_state->sparse_runtime, command_buffer, _swapchain);
    _state->progressive_runtime.Dispatch(_state->progressive_runtime, command_buffer, _swapchain);

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
          vktk::Context::CompileShader_GLSLtoSPIRV(
              VK_SHADER_STAGE_VERTEX_BIT, "render_region_vertex",
              { "#version 450",
                "#define CW_CUBE",
                #include "shaders/cube_vertices.vert.h"
                ,
                #include "shaders/draw_cube.vert.h"
              }
          )
        },
        { VK_SHADER_STAGE_FRAGMENT_BIT,
          vktk::Context::CompileShader_GLSLtoSPIRV(
              VK_SHADER_STAGE_FRAGMENT_BIT, "render_region_fragment",
              { "#version 450",
                #include "shaders/voxel_traversal.h"
                ,
                #include "shaders/render_region.frag.h"
              }
          )
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
          vktk::Context::CompileShader_GLSLtoSPIRV(
              VK_SHADER_STAGE_VERTEX_BIT, "sparse_region_vertex",
              { "#version 450",
                "#define CW_CUBE",
                #include "shaders/cube_vertices.vert.h"
                ,
                #include "shaders/draw_cube.vert.h"
              }
          )
        },
        { VK_SHADER_STAGE_FRAGMENT_BIT, {
            #include "shaders/sparse_region.frag.glsl.spv"
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
              VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT, 512*128 },
        }
    });

    output.binding_groups.push_back(vkrg::layout::StageBindingGroup{
        {
            { VK_SHADER_STAGE_FRAGMENT_BIT, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
              VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT, 512*32 },
        }
    });

    output.binding_groups.push_back(vkrg::layout::StageBindingGroup{
        {
            { VK_SHADER_STAGE_FRAGMENT_BIT, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
              VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT, 512*128 },
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
          vktk::Context::CompileShader_GLSLtoSPIRV(
              VK_SHADER_STAGE_VERTEX_BIT, "skybox_vertex",
              { "#version 450",
                #include "shaders/full_screen.vert.h"
              }
          )
        },
        { VK_SHADER_STAGE_FRAGMENT_BIT,
          vktk::Context::CompileShader_GLSLtoSPIRV(
              VK_SHADER_STAGE_FRAGMENT_BIT, "skybox_fragment",
              { "#version 450",
                #include "shaders/skybox.frag.h"
              }
          )
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
          vktk::Context::CompileShader_GLSLtoSPIRV(
              VK_SHADER_STAGE_VERTEX_BIT, "resolve_vertex",
              { "#version 450",
                #include "shaders/full_screen.vert.h"
              }
          )
        },
        { VK_SHADER_STAGE_FRAGMENT_BIT,
          vktk::Context::CompileShader_GLSLtoSPIRV(
              VK_SHADER_STAGE_FRAGMENT_BIT, "resolve_fragment",
              { "#version 450",
                #include "shaders/resolve.frag.h"
              }
          )
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
          vktk::Context::CompileShader_GLSLtoSPIRV(
              VK_SHADER_STAGE_VERTEX_BIT, "guiframes_vertex",
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
          )
        },

        { VK_SHADER_STAGE_FRAGMENT_BIT,
          vktk::Context::CompileShader_GLSLtoSPIRV(
              VK_SHADER_STAGE_FRAGMENT_BIT, "guiframes_fragment",
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
          )
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
          vktk::Context::CompileShader_GLSLtoSPIRV(
              VK_SHADER_STAGE_VERTEX_BIT, "guiglyphs_vertex",
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
          )
        },

        { VK_SHADER_STAGE_FRAGMENT_BIT,
          vktk::Context::CompileShader_GLSLtoSPIRV(
              VK_SHADER_STAGE_FRAGMENT_BIT, "guiglyphs_fragment",
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
          )
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
          vktk::Context::CompileShader_GLSLtoSPIRV(
              VK_SHADER_STAGE_VERTEX_BIT, "progressive-resolve_vertex",
              { "#version 450",
                #include "shaders/full_screen.vert.h"
              }
          )
        },
        { VK_SHADER_STAGE_FRAGMENT_BIT,
          vktk::Context::CompileShader_GLSLtoSPIRV(
              VK_SHADER_STAGE_FRAGMENT_BIT, "progressive-resolve_fragment",
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
          )
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
                  numtk::vec4f{ 0.f, 0.f, 0.f, 0.f },
                  VK_ATTACHMENT_LOAD_OP_LOAD,
                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                }
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

    _registry.BindComponent(graph.MakeWeakEdge("FramebufferBlit", "FramebufferResolve"),
                            vkrg::layout::EdgeBindings{
                                vkrg::layout::EdgeType_Image,
                                vkrg::layout::BindingPoint{
                                    .type = vkrg::layout::BindingType_BlitImage,
                                    .blit_slot = vkrg::layout::BlitImageSlot_Source
                                },
                                vkrg::layout::BindingPoint{
                                    .type = vkrg::layout::BindingType_Framebuffer,
                                    .attachment_index = 0
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
                },
                {
                    VK_IMAGE_TYPE_2D, VK_FORMAT_D32_SFLOAT,
                    vkrg::layout::ClearValue{ 1.f, 0u }
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

struct SparsePipelinePayload
{
    bool region_ready = false;
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

void SparseRegionUpdateBindings(dispatch_runtime_t& _rt, VoxelRegionPackage const& _region_package)
{
    SparsePipelinePayload& payload = *(SparsePipelinePayload*)_rt.payload;

    {
        vkrg::bind::PNextMap pNext_map = {};

        static uint32_t desc_counts[3] = { 0u, 0u, 0u };
        desc_counts[0] = (uint32_t)_region_package.masks.size();
        desc_counts[1] = (uint32_t)_region_package.offsets.size();
        desc_counts[2] = (uint32_t)_region_package.payloads.size();

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

    for (vktk::Texture const& node : _region_package.masks)
    {
        VkDescriptorImageInfo region_image_info{};
        region_image_info.sampler = payload.nearest_sampler;
        region_image_info.imageView = node.view;
        region_image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        nodes_info.push_back(region_image_info);
    }

    for (vktk::Texture const& node_payload : _region_package.payloads)
    {
        VkDescriptorImageInfo payload_image_info{};
        payload_image_info.sampler = payload.nearest_sampler;
        payload_image_info.imageView = node_payload.view;
        payload_image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        payloads_info.push_back(payload_image_info);
    }

    for (vktk::Texture const& offsets : _region_package.offsets)
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

    payload.region_ready = true;
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
                | VK_IMAGE_USAGE_SAMPLED_BIT,
                {
                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_ACCESS_COLOR_ATTACHMENT_READ_BIT,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
                },
                [&_swapchain](void* buffer) {
                    std::memset(buffer, 0,
                                _swapchain.size.width * _swapchain.size.height * 16);
                }
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

        guitk::TextureData const& font_glyphs = guitk::FontBitmap();
        payload.gui_font_texture = _rt.vulkan->CreateTexture(
            font_glyphs.size.x(), font_glyphs.size.y(), 1,
            VK_IMAGE_TYPE_2D,
            VK_FORMAT_R8_UINT,
            VK_IMAGE_USAGE_SAMPLED_BIT,
            [&font_glyphs](void* buffer) {
                uint8_t* texture_buffer = (uint8_t*)buffer;
                std::copy(font_glyphs.pixels.begin(), font_glyphs.pixels.end(), texture_buffer);
            }
        );

        guitk::TextureData const& symbols = guitk::SymbolsBitmap();
        payload.gui_symbols_texture = _rt.vulkan->CreateTexture(
            symbols.size.x(), symbols.size.y(), 1,
            VK_IMAGE_TYPE_2D,
            VK_FORMAT_R8_UINT,
            VK_IMAGE_USAGE_SAMPLED_BIT,
            [&symbols](void* buffer) {
                uint8_t* texture_buffer = (uint8_t*)buffer;
                std::copy(symbols.pixels.begin(), symbols.pixels.end(), texture_buffer);
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

#ifdef SYNC_LOAD
        SparseRegionUpdateBindings(_rt, _rt.state->region_package);
#endif
    };

    runtime.RenderResources = [](dispatch_runtime_t& _rt,
                                 vktk::Swapchain const& _swapchain)
    {};

    runtime.Update = [](dispatch_runtime_t& _rt)
    {
#ifndef SYNC_LOAD
        if (_rt.state->region_future.valid()
            && (_rt.state->region_future.wait_for(std::chrono::seconds(0))
                == std::future_status::ready))
        {
            VoxelRegionFuture voxel_region = _rt.state->region_future.get();
            _rt.state->region_package = UploadVoxelRegion(_rt.vulkan,
                                                          voxel_region.region);
            SparseRegionUpdateBindings(_rt, _rt.state->region_package);
        }
#endif
    };

    runtime.Dispatch = [](dispatch_runtime_t& _rt,
                          vktk::CommandBuffer& _command_buffer,
                          vktk::Swapchain const& _swapchain)
    {
        SparsePipelinePayload& payload = *(SparsePipelinePayload*)_rt.payload;

        if (!payload.region_ready)
            return;

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
                256.f, 0.f, 0.f, 0.f,
                0.f, 256.f, 0.f, 0.f,
                0.f, 0.f, 256.f, 0.f,
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
        push_constants->node_size = (float)VOXEL_NODE_SIZE;
        push_constants->region_scale = _rt.state->region_package.area_scale;
        push_constants->stack_depth = std::max(_rt.state->region_package.display_depth-1, 0);
        push_constants->sample_offset = _rt.state->progressive_frame_index;
        push_constants->leaves_begin = _rt.state->region_package.leaves_begin;
        push_constants->sun_power = _rt.state->sun_power;
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
