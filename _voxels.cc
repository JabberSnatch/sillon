#define VOXTK_IMPLEMENTATION
#define VOXTK_IMPORT_IMPLEMENTATION
#define ZIPTK_IMPLEMENTATION
#define MCTK_IMPLEMENTATION

#include "_voxels.hh"

#include "_voxels/atmosphere_runtime.hh"
#include "_voxels/progressive_runtime.hh"
#include "_voxels/voxel_field_runtime.hh"
#include "_voxels/gui_runtime.hh"

#include "mctk/mctk.hh"

#include <numtk.hh>
#include <dstk.hh>
#include <plltk.hh>

#include "guitk.hh"
#include "systk.hh"
#include "vktk.hh"

#include <voxtk.hh>
#include "voxtk-importers/voxtk_import.hh"

#include <bit>
#include <chrono>
#include <format>
#include <future>
#include <memory>
#include <unordered_map>

//#define VOXEL_CHUNK_SIZE 16u

namespace {

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
#if 0
    numtk::vec2f forward_spherical = { 0.710f, -0.260f };
    numtk::vec3f position = { 37.025f, -36.243f, -18.561f };
#endif
#if 0
    numtk::vec2f forward_spherical = { -4.73f, -1.26f };
    numtk::vec3f position = { 222.36f, -152.f, -129.64f };
#endif
#if 0
    numtk::vec2f forward_spherical = { -9.45f, -1.57f };
    numtk::vec3f position = { 248.72f, 223.71f, 152.74f };
#endif
    numtk::vec2f forward_spherical = { -11.58f, 0.4f };
    numtk::vec3f position = { -0.8f, -215.33f, 86.65f };

    float velocity = 5.f;

    void Update(iotk::input_t const* _input,
                iotk::input_t const* _past_input);
    numtk::mat4f Matrix() const;
    bool updated;
};

std::unique_ptr<voxel_field_runtime::FieldDescriptor>
PrepareRegionUpload(vktk::Context* vulkan,
                    voxtk::VoxelField const& region);

voxel_field_runtime::RenderParams VoxelFieldSetupRender(uint32_t chunk_size);

voxtk::VoxelField VoxelFieldDownsample(voxtk::VoxelField const& source, uint32_t size_factor);
uint32_t VoxelFieldRenderLayout(voxtk::VoxelField const& source,
                                voxtk::VoxelField const& target,
                                voxel_field_runtime::RenderParams const& render_params,
                                uint32_t base_offset,
                                uint32_t lod_index,
                                std::vector<uint64_t*> const& masks,
                                std::vector<uint32_t*> const& offsets);

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
    float fov = 90.f;

    //numtk::vec2f sun_spherical = { 5.71f, 0.84f };
    //numtk::vec2f sun_spherical = { 6.43f, 0.02f };
    numtk::vec2f sun_spherical = { 6.43f, -0.02f };
    //numtk::vec2f sun_spherical = { 6.46f, 0.01f };
    //numtk::vec2f sun_spherical = { 0.71f, .7f };

    uint32_t frame_index = 0;

    uint32_t progressive_frame_index = 0;

    dstk::Registry core_registry;
    dispatch_runtime_t progressive_runtime;
    dispatch_runtime_t transmittance_runtime;

    VkFence last_submit;

    mutable std::future<VoxelRegionFuture> region_future = {};
    std::unique_ptr<voxtk::VoxelField> voxel_region{ nullptr };
    std::unique_ptr<voxel_field_runtime::FieldDescriptor> field_desc{ nullptr };
};

#if 0
struct MatrixBlock
{
    numtk::mat4f projection;
    numtk::mat4f view_model;
    numtk::mat4f view;
    numtk::vec2f screen_offset;
    numtk::vec2i screen_size;
    float padding[12];
};

struct SkyboxPushConstants
{
    numtk::mat4f projection;
    numtk::mat4f view;
    numtk::vec4f position;
    numtk::vec3f light_dir;
};
#endif

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
    output.SetField({ 704, 0, 0 }, copy);
    output.SetField({ 0, 1000, 0 }, copy);
    output.SetField({ 704, 1000, 0 }, copy);

    output.SetField({ 0, 0, 1496 }, copy);
    output.SetField({ 704, 0, 1496 }, copy);
    output.SetField({ 0, 1000, 1496 }, copy);
    output.SetField({ 704, 1000, 1496 }, copy);
#endif

#if 0
    output.SetField({ 0, 0, 704 }, copy);
    output.SetField({ 704, 0, 704 }, copy);
    output.SetField({ 0, 1000, 704 }, copy);
    output.SetField({ 704, 1000, 704 }, copy);
#endif

    //output.SetField({ 4096, 0, 4096 }, copy);

#if 0
    output.SetField({ 512, 512, 0 }, copy);
    output.SetField({ 0, 512, 512 }, copy);
    output.SetField({ 512, 512, 512 }, copy);
#endif
#if 0
    for (int32_t index = 1; index < 16; ++index)
    {
        output.SetField({ 512 * (index%4), 0, 512 * (index/4) }, copy);
        if (((index%4) & (index/4)) & 1)
            output.SetField({ 0, 512 * 3, 0 }, copy);
    }
#endif
    return output;
}


std::unique_ptr<voxel_field_runtime::FieldDescriptor>
PrepareRegionUpload(vktk::Context* vulkan,
                    voxtk::VoxelField const& region)
{
    systk::Timer scope_time{ "PrepareRegionUpload",
        [](std::string const& tag, float time) {
            std::cout << "[" << tag << "] " << time << " s" << std::endl;
        }
    };

    constexpr uint32_t kChunkSize = VOXEL_CHUNK_SIZE;
    constexpr uint32_t kKernelSize = voxtk::VoxelMask::kKernelSize;
    constexpr uint32_t kLevelSize = kChunkSize*kKernelSize;

    uint32_t log_level_size = (uint32_t)numtk::BitTrailingZeroCount(kLevelSize);
    numtk::vec3u area_size = region.bounds.extent().cast<uint32_t>();
    numtk::vec3u log_area_size{
        (uint32_t)(64 - numtk::BitLeadingZeroCount(area_size.x - 1)),
        (uint32_t)(64 - numtk::BitLeadingZeroCount(area_size.y - 1)),
        (uint32_t)(64 - numtk::BitLeadingZeroCount(area_size.z - 1))
    };

    uint32_t depth = [&](){
        uint32_t max_log_area_size = std::max(log_area_size.x,
                                              std::max(log_area_size.y, log_area_size.z));
        uint32_t max_area_size = (std::max(area_size.x, std::max(area_size.y, area_size.z))
                                  + (kKernelSize-1)) / kKernelSize;

        uint32_t depth = (uint32_t)((max_log_area_size + (log_level_size-1)) / log_level_size);
        assert(depth < 4);

        return depth;
    }();

    std::unique_ptr<voxel_field_runtime::FieldDescriptor> output{ new voxel_field_runtime::FieldDescriptor{} };

    voxel_field_runtime::FieldDescriptor& package = *output;
    package.stack_depth = depth*2;
    package.display_depth = package.stack_depth;
    package.area_scale = numtk::vec3f::Constant(1.f) / numtk::vec3f{
        (float)(1u << ((log_level_size * depth) - log_area_size.x)),
        (float)(1u << ((log_level_size * depth) - log_area_size.y)),
        (float)(1u << ((log_level_size * depth) - log_area_size.z))
    };
    package.area_size = (numtk::vec3u::Constant(1) << log_area_size).cast<float>();

    voxel_field_runtime::FieldUpload upload = {};
    upload.base = &region;
    upload.levels.resize(depth+1);
    upload.render_params = VoxelFieldSetupRender(kChunkSize);
    upload.package = output.get();

    voxtk::VoxelField const* src = &region;
    for (uint32_t lod_index = 0; lod_index < depth; ++lod_index)
    {
        upload.lods.push_back(VoxelFieldDownsample(*src, kChunkSize));
        src = &upload.lods[lod_index];
    }

    for (uint32_t index = 1u; index < depth; ++index)
    {
        uint32_t lod_index = (depth-1) - index;
        voxel_field_runtime::FieldLevel& level = upload.levels[lod_index+1];
        voxtk::VoxelField const& source = upload.lods[lod_index];
        voxtk::VoxelField const& target = upload.lods[lod_index+1];
        uint64_t target_count = target.Popcount();

        level.masks.resize(target_count);
        level.offsets.resize(target_count);

        for (uint64_t point_index = 0ull; point_index < target_count; ++point_index)
        {
            package.masks.push_back(vulkan->CreateTexture(
                                        kChunkSize, kChunkSize, kChunkSize,
                                        VK_IMAGE_TYPE_3D, VK_FORMAT_R32G32_UINT,
                                        VK_IMAGE_USAGE_SAMPLED_BIT,
                                        vktk::ImageSyncTriplet{
                                            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                            VK_ACCESS_SHADER_READ_BIT,
                                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                        },
                                        level.masks[point_index]
                                    ));

            package.offsets.push_back(vulkan->CreateTexture(
                                          kChunkSize, kChunkSize, kChunkSize,
                                          VK_IMAGE_TYPE_3D, VK_FORMAT_R32_UINT,
                                          VK_IMAGE_USAGE_SAMPLED_BIT,
                                          vktk::ImageSyncTriplet{
                                            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                            VK_ACCESS_SHADER_READ_BIT,
                                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                          },
                                          level.offsets[point_index]
                                      ));
        }
    }

    {
        package.leaves_begin = package.masks.size();

        uint64_t level1_popcount = upload.lods[0].Popcount();
        voxel_field_runtime::FieldLevel& level = upload.levels[0];

        level.masks.resize(level1_popcount);
        if (region.HasLayer(0))
            level.payloads.resize(level1_popcount);

        for (uint64_t point_index = 0; point_index < level1_popcount; ++point_index)
        {
            package.masks.push_back(vulkan->CreateTexture(
                                        kChunkSize, kChunkSize, kChunkSize,
                                        VK_IMAGE_TYPE_3D, VK_FORMAT_R32G32_UINT,
                                        VK_IMAGE_USAGE_SAMPLED_BIT,
                                        vktk::ImageSyncTriplet{
                                            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                            VK_ACCESS_SHADER_READ_BIT,
                                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                        },
                                        level.masks[point_index]
                                    ));

            if (region.HasLayer(0))
            {
                package.payloads.push_back(
                    vulkan->CreateTexture(
                        upload.render_params.NodeSize(),
                        upload.render_params.NodeSize(),
                        upload.render_params.NodeSize(),
                        VK_IMAGE_TYPE_3D, VK_FORMAT_R8G8B8A8_UNORM,
                        VK_IMAGE_USAGE_SAMPLED_BIT,
                        vktk::ImageSyncTriplet{
                            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                            VK_ACCESS_SHADER_READ_BIT,
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                        },
                        level.payloads[point_index]));
            }
        }
    }

    package.residency_entry_count = ((package.masks.size() + 31) / 32);

    package.residency_staging = vulkan->CreateBuffer(
        sizeof(uint32_t) * package.residency_entry_count,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
        | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    package.residency_buffer = vulkan->CreateBuffer(
        sizeof(uint32_t) * package.residency_entry_count,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        [entry_count = package.residency_entry_count](void* _ptr){
            uint32_t* buffer = (uint32_t*)_ptr;
            std::memset(buffer, 0, entry_count * sizeof(uint32_t));
        });

    package.residency_state.resize(package.residency_entry_count);

    package.upload = std::async(
        std::launch::async,
        [](voxel_field_runtime::FieldUpload args) -> voxel_field_runtime::FieldUpload {

            systk::Timer scope_time{ "RegionCopy",
                [](std::string const& tag, float time) {
                    std::cout << "[" << tag << "] " << time << " s" << std::endl;
                }
            };

            uint32_t depth = (uint32_t)args.lods.size();
            uint32_t base_offset = 1u;
            uint32_t current_offset = 0u;

            for (uint32_t index = 1u; index < depth; ++index)
            {
                uint32_t lod_index = (depth-1) - index;
                voxtk::VoxelField const& source = args.lods[lod_index];
                voxtk::VoxelField const& target = args.lods[lod_index+1];

                uint64_t target_count = target.Popcount();
                std::vector<uint32_t*> offsets = {};
                std::vector<uint64_t*> masks = {};
                offsets.resize(target_count);
                masks.resize(target_count);

                for (uint64_t point_index = 0ull; point_index < target_count; ++point_index)
                {
                    masks[point_index] =
                        (uint64_t*)args.levels[lod_index+1].masks[point_index].staging.alloc.map;
                    offsets[point_index] =
                        (uint32_t*)args.levels[lod_index+1].offsets[point_index].staging.alloc.map;
                }

                uint32_t node_count = VoxelFieldRenderLayout(source,
                                                      target,
                                                      args.render_params,
                                                      base_offset,
                                                      index,
                                                      masks,
                                                      offsets);

                {
                    plltk::ScopeLock lock{ *args.package->residency_mutex };
                    for (uint64_t point_index = 0ull; point_index < target_count; ++point_index)
                    {
                        uint32_t mask_index = current_offset + (uint32_t)point_index;
                        args.package->residency_state[mask_index/32] |= 1u << mask_index%32;
                        args.package->pending_uploads->push_back(
                            &args.levels[lod_index+1].masks[point_index]);
                        args.package->pending_uploads->push_back(
                            &args.levels[lod_index+1].offsets[point_index]);
                    }
                }

                current_offset = base_offset;
                base_offset += node_count;
            }

            {
                uint32_t shift_depth = args.render_params.LevelShift() * (depth-1);
                uint32_t level1_count = 1 << shift_depth;
                uint64_t level1_popcount = args.lods[0].Popcount();

                std::vector<uint64_t*> masks = {};
                std::vector<numtk::vec4<uint8_t>*> payloads = {};

                masks.resize(level1_popcount);
                if (!args.levels[0].payloads.empty())
                    payloads.resize(level1_popcount);

                for (uint64_t point_index = 0; point_index < level1_popcount; ++point_index)
                {
                    masks[point_index] =
                        (uint64_t*)args.levels[0].masks[point_index].staging.alloc.map;
                    if (!payloads.empty())
                        payloads[point_index] =
                            (numtk::vec4<uint8_t>*)args.levels[0].payloads[point_index].staging.alloc.map;
                }

                voxtk::VoxelField const& region = *args.base;
                uint32_t node_size = args.render_params.NodeSize();
                uint64_t buffer_index = 0ull;
                for (uint32_t point_index = 0u; point_index < level1_count; ++point_index)
                {
                    numtk::vec3u const lod1_base = args.render_params.IndexPoint(point_index);
                    if (!args.lods[0].Test(lod1_base.cast<int32_t>()))
                        continue;

                    numtk::vec3u const lod0_base = lod1_base * node_size;

                    uint64_t* mask = masks[buffer_index];
                    numtk::vec4<uint8_t>* payload = !payloads.empty()
                        ? payloads[buffer_index]
                        : nullptr;

                    for (uint32_t srcz = 0; srcz < kChunkSize; ++srcz)
                    {
                        for (uint32_t srcy = 0; srcy < kChunkSize; ++srcy)
                        {
                            for (uint32_t srcx = 0; srcx < kChunkSize; ++srcx)
                            {
                                numtk::vec3 kernel_base = numtk::vec3u{
                                    srcx, srcy, srcz
                                } * numtk::vec3u::Constant(voxtk::VoxelMask::kKernelSize)
                                      + lod0_base;

                                uint64_t bits = region.TestKernel(kernel_base.cast<int32_t>());
                                mask[srcx + srcy*kChunkSize + srcz*kChunkSize*kChunkSize] = bits;

                                if (payload)
                                {
                                    for (uint32_t bit_index = 0;
                                         bit_index < voxtk::VoxelMask::kKernelVolume;
                                         ++bit_index)
                                    {
                                        numtk::vec3u voxel_base = kernel_base
                                            + args.render_params.IndexPoint(bit_index);
                                        numtk::vec3u texture_base = voxel_base - lod0_base;

                                        uint32_t payload_index =
                                            texture_base.x
                                            + texture_base.y*node_size
                                            + texture_base.z*node_size*node_size;

                                        numtk::vec3<uint8_t> const& data =
                                            region.LoadData<numtk::vec3<uint8_t>>(
                                                voxel_base.cast<int32_t>(),
                                                0);

                                        payload[payload_index] = numtk::vec4<uint8_t>{
                                            data.x, data.y, data.z, 0
                                        };
                                    }
                                }
                            }
                        }
                    }

                    {
                        plltk::ScopeLock lock{ *args.package->residency_mutex };
                        args.package->residency_state[current_offset/32] |= 1u << current_offset%32;
                        args.package->pending_uploads->push_back(
                            &args.levels[0].masks[buffer_index]);
                        if (!payloads.empty())
                            args.package->pending_uploads->push_back(
                                &args.levels[0].payloads[buffer_index]);
                    }

                    ++buffer_index;
                    ++current_offset;
                }
            }

            return std::move(args);
        },
        std::move(upload)
    );

    return std::move(output);
}

voxel_field_runtime::RenderParams VoxelFieldSetupRender(uint32_t chunk_size)
{
    voxel_field_runtime::RenderParams output = {};
    output.chunk_size = chunk_size;
    output.chunk_mask = output.chunk_size - 1u;
    output.chunk_shift = (uint32_t)std::popcount(output.chunk_mask);
    output.kernel_mask = voxtk::VoxelMask::kKernelSize - 1;
    output.kernel_shift = (uint32_t)std::popcount(output.kernel_mask);

    uint32_t comp_shift = output.chunk_shift + output.kernel_shift;
    uint32_t level_shift = comp_shift * 3;
    uint32_t chunk_aggregate = 0u;
    uint32_t kernel_aggregate = 0u;

    for (uint32_t index = 0u; index < (32/level_shift)+1; ++index)
    {
        chunk_aggregate = (chunk_aggregate << level_shift) |
            (output.chunk_mask << (3 * output.kernel_shift));
        kernel_aggregate = (kernel_aggregate << level_shift) |
            (output.kernel_mask);
    }

    output.indexing_masks = numtk::vec3u{
        chunk_aggregate | kernel_aggregate,
        (chunk_aggregate << output.chunk_shift) | (kernel_aggregate << output.kernel_shift),
        (chunk_aggregate << (output.chunk_shift * 2)) | (kernel_aggregate << (output.kernel_shift * 2))
    };

    return output;
}

voxtk::VoxelField VoxelFieldDownsample(voxtk::VoxelField const& source, uint32_t size_factor)
{
    voxtk::VoxelField target{};
    uint32_t downsample_size = size_factor * voxtk::VoxelMask::kKernelSize;

    numtk::bounds3i range = [&](){
        numtk::bounds3i range = voxtk::VoxelField::Node::AlignedBounds(source.bounds, 0);
        numtk::vec3i down_min = (range.min + numtk::vec3i::Constant(downsample_size-1))
            / downsample_size;
        numtk::vec3i down_max = (range.max + numtk::vec3i::Constant(downsample_size-1))
            / downsample_size;
        return numtk::bounds3i::MinExtent(
            down_min,
            numtk::max(down_max - down_min, numtk::vec3i::Constant(1))
        );
    }();

    static const auto ComputeBitPredicate = [](voxtk::VoxelField const& source,
                                               uint32_t size_factor,
                                               numtk::vec3i const& base) {

        for (int32_t srcz = 0; srcz < size_factor; ++srcz)
        {
            for (int32_t srcy = 0; srcy < size_factor; ++srcy)
            {
                for (int32_t srcx = 0; srcx < size_factor; ++srcx)
                {
                    numtk::vec3i kernel_base = base +
                        numtk::vec3i{ srcx, srcy, srcz } * voxtk::VoxelMask::kKernelSize;

                    uint64_t source_bits = source.TestKernel(kernel_base);
                    if (source_bits != 0)
                        return true;
                }
            }
        }

        return false;
    };

    for (int32_t tgtz = range.min.z; tgtz < range.max.z; ++tgtz)
    {
        for (int32_t tgty = range.min.y; tgty < range.max.y; ++tgty)
        {
            for (int32_t tgtx = range.min.x; tgtx < range.max.x; ++tgtx)
            {
                numtk::vec3i target_base{ tgtx, tgty, tgtz };
                numtk::vec3i source_base = target_base * downsample_size;

                bool bit = ComputeBitPredicate(source, size_factor, source_base);

                target.Set(target_base, bit);
            }
        }
    }

    return target;
}

uint32_t VoxelFieldRenderLayout(voxtk::VoxelField const& source,
                                voxtk::VoxelField const& target,
                                voxel_field_runtime::RenderParams const& render_params,
                                uint32_t base_offset,
                                uint32_t lod_index,
                                std::vector<uint64_t*> const& masks,
                                std::vector<uint32_t*> const& offsets)
{
    assert(masks.size() == offsets.size());

    uint32_t downsample_size = render_params.NodeSize();
    numtk::bounds3i range = voxtk::VoxelField::Node::AlignedBounds(target.bounds, 0);

    static const auto VoxelRenderNode = [](voxtk::VoxelField const& source,
                                           uint32_t chunk_size,
                                           numtk::vec3i const& base,
                                           uint32_t base_offset,
                                           uint64_t* masks,
                                           uint32_t* offsets) {
        uint32_t count = 0u;

        for (int32_t srcz = 0; srcz < chunk_size; ++srcz)
        {
            for (int32_t srcy = 0; srcy < chunk_size; ++srcy)
            {
                for (int32_t srcx = 0; srcx < chunk_size; ++srcx)
                {
                    numtk::vec3i kernel_base = base +
                        numtk::vec3i{ srcx, srcy, srcz } * voxtk::VoxelMask::kKernelSize;
                    uint32_t point_index = srcx + srcy*chunk_size + srcz*chunk_size*chunk_size;

                    uint64_t bits = source.TestKernel(kernel_base);
                    masks[point_index] = bits;
                    offsets[point_index] = count + base_offset;
                    count += std::popcount(bits);
                }
            }
        }

        return count;
    };

    assert(target.Popcount() == offsets.size());
    uint32_t buffer_index = 0u;
    uint32_t current_offset = 0u;

    numtk::vec3u range_extent = lod_index == 0
        ? numtk::vec3u::Constant(1)
        : range.extent().cast<uint32_t>();

    uint32_t max_index = render_params.PointIndex(range_extent - numtk::vec3u::Constant(1));

    for (uint32_t point_index = 0u; point_index < max_index+1; ++point_index)
    {
        numtk::vec3u indexing_base = render_params.IndexPoint(point_index);

#define BIGMIN_SKIP
#ifdef BIGMIN_SKIP
        if (indexing_base.x >= range_extent.x
            || indexing_base.y >= range_extent.y
            || indexing_base.z >= range_extent.z)
        {
            uint32_t bigmin = 0;

            uint32_t zval = point_index;
            uint32_t zmin = 0;
            uint32_t zmax = max_index;

            uint32_t pack_count = 6 * lod_index;

            static auto const LoadZeros = [](uint32_t v, uint32_t max_index,
                                             uint32_t mask, uint32_t stride) {
                for (uint32_t index = 0; index < max_index; ++index)
                    v &= ~(mask << (stride * index));
                v |= (mask << (stride*max_index));
                return v;
            };

            static auto const LoadOnes = [](uint32_t v, uint32_t max_index,
                                            uint32_t mask, uint32_t stride) {
                for (uint32_t index = 0; index < max_index; ++index)
                    v |= (mask << (stride * index));
                v &= ~(mask << (stride*max_index));
                return v;
            };

            for (uint32_t index = 0; index < pack_count; ++index)
            {
                uint32_t pack_index = pack_count-1 - index;
                uint32_t pack_mask = ((pack_index / 3) & 1)
                    ? render_params.chunk_mask
                    : render_params.kernel_mask;
                uint32_t pack_shift = ((pack_index / 3) & 1)
                    ? render_params.chunk_shift
                    : render_params.kernel_shift;

                uint32_t base_shift = render_params.LevelShift() * (pack_index/6);
                uint32_t comp_shift = pack_shift * (pack_index%3);
                if (pack_index % 6 >= 3)
                    comp_shift += render_params.kernel_shift*3;

                uint32_t mask = pack_mask << (base_shift + comp_shift);

                uint32_t val_bit = zval & mask;
                uint32_t min_bit = zmin & mask;
                uint32_t max_bit = zmax & mask;

                uint32_t branch_index =
                    (val_bit ? 1 : 0) +
                    (min_bit && (val_bit >= min_bit) ? 2 : 0) +
                    (max_bit && (val_bit <= max_bit) ? 4 : 0);

                switch (branch_index)
                {

                case 0:
                    break;

                case 1: // val_bit
                    index = pack_count;
                    break;

                case 2: // min_bit
                    assert(false);
                    break;

                case 3: // val_bit && min_bit
                    assert(false);
                    break;

                case 4: // max_bit
                    bigmin = LoadZeros(zmin, (pack_index/6),
                                       pack_mask << comp_shift, render_params.LevelShift());
                    zmax = LoadOnes(zmax, (pack_index/6),
                                    pack_mask << comp_shift, render_params.LevelShift());
                    break;

                case 5: // val_bit && max_bit
                    zmin = LoadZeros(zmin, (pack_index/6),
                                     pack_mask << comp_shift, render_params.LevelShift());
                    break;

                case 6: // min_bit && max_bit
                    bigmin = zmin;
                    index = pack_count;
                    break;

                case 7: // val_bit && min_bit && max_bit
                    break;

                default: break;
                }
            }

            uint32_t popcount = std::popcount(bigmin);
            uint32_t point_count = (1u << popcount);
            uint32_t base_mask = bigmin;
            for (uint32_t index = 0u; index < point_count; ++index)
            {
                uint32_t reverse_index = point_count-1 - index;

                uint32_t next_min = numtk::BitDeposit(reverse_index, base_mask);

                if (next_min < point_index)
                    break;

                numtk::vec3u test_point = render_params.IndexPoint(next_min);

                if (test_point.x < range_extent.x &&
                    test_point.y < range_extent.y &&
                    test_point.z < range_extent.z)
                    bigmin = next_min;
            }

            point_index = bigmin;
            if (point_index == 0)
                break;

            indexing_base = render_params.IndexPoint(point_index);
        }
#endif

        numtk::vec3i target_base = range.min + indexing_base.cast<int32_t>();

        if (!target.Test(target_base))
            continue;

        numtk::vec3i source_base = target_base * downsample_size;
        current_offset += VoxelRenderNode(source,
                                          render_params.chunk_size,
                                          source_base,
                                          current_offset + base_offset,
                                          masks[buffer_index],
                                          offsets[buffer_index]);

        ++buffer_index;
    }

    assert(buffer_index == offsets.size());
    return current_offset;
}

void ConstantResources(void* _pstate, vktk::Context* _vulkan)
{
    voxel_state_t* _state = (voxel_state_t*)_pstate;
    _state->gui_context = guitk::CreateContext();

    _state->transmittance_runtime = atmosphere_runtime::Create(_vulkan, &_state->core_registry);
    _state->transmittance_runtime.OnModuleScopeOpen(_state->transmittance_runtime);
    {
        atmosphere_runtime::Args args = {};
        args.transmittance_size = numtk::vec2i{ 512, 512 };
        args.transmittance_texture = _vulkan->CreateTexture(
            args.transmittance_size.x(), args.transmittance_size.y(), 1,
            VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
            | VK_IMAGE_USAGE_SAMPLED_BIT);

        args.irradiance_size = numtk::vec2i{ 512, 256 };
        args.irradiance_texture = _vulkan->CreateTexture(
            args.irradiance_size.x(), args.irradiance_size.y(), 1,
            VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
            | VK_IMAGE_USAGE_SAMPLED_BIT);
        args.baseline_texture = _vulkan->CreateTexture(
            args.irradiance_size.x(), args.irradiance_size.y(), 1,
            VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
            | VK_IMAGE_USAGE_SAMPLED_BIT);

        numtk::vec3f sun_dir = numtk::vec3f{
            std::cos(_state->sun_spherical[1]) * std::cos(_state->sun_spherical[0]),
            std::sin(_state->sun_spherical[1]),
            std::cos(_state->sun_spherical[1]) * std::sin(_state->sun_spherical[0])
        }.normalise();

        args.light_dir = sun_dir;
        args.altitude = 50.f;

        vktk::CommandBuffer command_buffer = _vulkan->CreateCommandBuffer(_vulkan->present_queue);
        command_buffer.Begin();
        _state->transmittance_runtime.Dispatch(_state->transmittance_runtime,
                                               &args,
                                               command_buffer);
        command_buffer.End();
        command_buffer.Submit();
    }

    _state->progressive_runtime = progressive_runtime::Create(_vulkan, &_state->core_registry);

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
            voxtk::VoxelField temp{};

            {
                systk::Timer scope_time{ "COPC import",
                    [](std::string const& tag, float time) {
                        std::cout << "[" << tag << "] " << time << " s" << std::endl;
                    }
                };

#if 0
                numtk::vec2u area_dim{ 2, 2 };
                numtk::vec2u begin{ 431, 6714 };
                uint32_t voxel_density = 2048;
                std::string folder_prefix = "ANGERS_COPC";
                uint32_t z_offset = 0;
#endif

#if 0
                numtk::vec2u area_dim{ 3, 3 };
                numtk::vec2u begin{ 696, 6519 };
                uint32_t voxel_density = 2048;
                std::string folder_prefix = "PUYDEDOME_COPC";
                uint32_t z_offset = 0;
#endif

#if 1
                numtk::vec2u area_dim{ 12, 3 };
                numtk::vec2u begin{ 696, 6519 };
                uint32_t voxel_density = 1024;
                std::string folder_prefix = "PUYDEDOME_COPC";
                uint32_t z_offset = 0;
#endif

#if 0
                numtk::vec2u area_dim{ 3, 3 };
                numtk::vec2u begin{ 647, 6862 };
                uint32_t voxel_density = 2048;
                std::string folder_prefix = "PARIS_COPC";
                uint32_t z_offset = 0;
#endif

                uint32_t upload_count = area_dim.x() * area_dim.y();
                std::vector<std::future<voxtk::VoxelField>> futures(upload_count);

                uint32_t const batch_size = 16;
                uint32_t upload_index = 0;
                while (upload_index < upload_count)
                {
                    std::cout << "Batch " << upload_index << " begin" << std::endl;

                    for (uint32_t batch_index = 0u;
                         batch_index < batch_size && upload_index + batch_index < upload_count;
                         ++batch_index)
                    {
                        uint32_t next_upload_index = upload_index + batch_index;
                        numtk::vec2u chunk_index{
                            next_upload_index % area_dim.x(),
                            next_upload_index / area_dim.x()
                        };

                        futures[next_upload_index] = std::async(
                            std::launch::async,
                            [chunk_index,
                             &area_dim, &begin,
                             &voxel_density, &folder_prefix,
                             &z_offset]() -> voxtk::VoxelField {

                                std::string region_string = std::format(
                                    "r_{}_{}", chunk_index.x(), chunk_index.y()
                                );

                                systk::Timer scope_time{
                                    region_string,
                                    [](std::string const& tag, float time) {
                                        std::cout << "[" << tag << "] " << time << " s" << std::endl;
                                    }
                                };

                                std::cout << "Import " << region_string << " begin" << std::endl;

                                numtk::vec2u global_chunk_index = begin + chunk_index;
                                std::string path = "../" + folder_prefix + "/"
                                    + std::format("{:0>4}_{:0>4}",
                                                  global_chunk_index.x(),
                                                  global_chunk_index.y())
                                    + ".copc.laz";
                                return voxtk::importer::ImportCOPC(path, voxel_density, z_offset);
                            }
                        );
                    }

                    for (uint32_t batch_index = 0u;
                         batch_index < batch_size && upload_index + batch_index < upload_count;
                         ++batch_index)
                    {
                        uint32_t next_upload_index = upload_index + batch_index;
                        numtk::vec2u chunk_index{
                            next_upload_index % area_dim.x(),
                            next_upload_index / area_dim.x()
                        };

                        numtk::vec3u offset{
                            chunk_index.x() * voxel_density,
                            0,
                            (area_dim.y() - 1 - chunk_index.y()) * voxel_density
                        };
                        futures[next_upload_index].wait();
                        temp.SetField(offset.cast<int32_t>(), futures[next_upload_index].get());
                        futures[next_upload_index] = std::future<voxtk::VoxelField>{};

                        std::string region_string = std::format(
                            "r_{}_{}", chunk_index.x(), chunk_index.y()
                        );
                        std::cout << "Import " << region_string << " end" << std::endl;
                    }

                    upload_index += batch_size;
                }

                output.region.SetField(-temp.bounds.min, temp);

#if 0
                output.region =
                    voxtk::importer::ImportCOPC(
                        "../LHD_FXX_0697_6520_PTS_LAMB93_IGN69.copc.laz",
                        //"../LHD_FXX_0697_6520_PTS_C_LAMB93_IGN69.copc.laz",
                        2048);//4096);
#endif
            }

            return std::move(output);
        }
    );
#endif

    _state->progressive_runtime.OnModuleScopeOpen(_state->progressive_runtime);

    return;
}

void RenderResources(void* _pstate,
                     vktk::Context* _vulkan,
                     vktk::Swapchain const& _swapchain)
{
    voxel_state_t* _state = (voxel_state_t*)_pstate;
    _state->progressive_runtime.OnSwapchainScopeOpen(_state->progressive_runtime, _swapchain);
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

        {
            std::string lightstr = "light : "
                + std::to_string(_state->sun_spherical.x()) + " "
                + std::to_string(_state->sun_spherical.y());
            guitk::Label(_state->gui_context, lightstr.c_str());
        }

        guitk::Checkbox(_state->gui_context, &_state->enable_indirect_light, "Indirect Lighting");

        if (guitk::Slider(_state->gui_context, &_state->sun_power, "Sun Power", 0.f, 1.f))
            reset_progressive_counter = true;

        if (guitk::Slider(_state->gui_context, &_state->fov, "Field Of View", 5.f, 150.f))
            reset_progressive_counter = true;

        guitk::EndFrame(_state->gui_context);

        guitk::WidgetStates const* gui_record = guitk::EndRecord(_state->gui_context);
        _state->current_gui_record = gui_record;
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
        _state->field_desc->display_depth = std::min(_state->field_desc->display_depth+1, _state->field_desc->stack_depth);
    if (_input->key_down['o'] && !_past_input->key_down['o'])
        _state->field_desc->display_depth = std::max(_state->field_desc->display_depth-1, 0);

    if (!_state->current_gui_record->mouse_over)
        _state->camera.Update(_input, _past_input);

    if (_state->camera.updated
        || (old_indirect_light_state != _state->enable_indirect_light)
        || reset_progressive_counter)
        _state->progressive_frame_index = 0;

    if (_state->region_future.valid()
        && (_state->region_future.wait_for(std::chrono::seconds(0))
            == std::future_status::ready))
    {
        VoxelRegionFuture voxel_region = _state->region_future.get();
        _state->voxel_region.reset(new voxtk::VoxelField{ std::move(voxel_region.region) });
        _state->field_desc = PrepareRegionUpload(_vulkan, *_state->voxel_region);
    }

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

    progressive_runtime::Args progressive_args = {};

    float how = (float)_swapchain.size.height / (float)_swapchain.size.width;

    progressive_args.voxel_field.field_descriptor = _state->field_desc.get();
    progressive_args.voxel_field.framebuffer_size = numtk::vec2f{
        (float)_swapchain.size.width,
        (float)_swapchain.size.height };
    progressive_args.voxel_field.view_matrix =
        _state->camera.Matrix();
    progressive_args.voxel_field.projection_matrix =
        numtk::mat4f::Perspective_VulkanNDC(1.0f, 1000000.f, _state->fov*numtk::kDegrees, how);
    progressive_args.voxel_field.sample_offset = _state->progressive_frame_index;
    progressive_args.voxel_field.sun_spherical = _state->sun_spherical;
    progressive_args.voxel_field.enable_indirect_light = _state->enable_indirect_light;
    progressive_args.voxel_field.sun_power = _state->sun_power;

    progressive_args.gui.gui_record = _state->current_gui_record;
    progressive_args.gui.framebuffer_size = numtk::vec2f{
        (float)_swapchain.size.width,
        (float)_swapchain.size.height };

    progressive_args.framebuffer_size = numtk::vec2u{
        _swapchain.size.width,
        _swapchain.size.height };
    progressive_args.swapchain_index = _swapchain.index;
    progressive_args.swapchain_image = _swapchain.images[_swapchain.index];
    progressive_args.progressive_frame_index = _state->progressive_frame_index;

    _state->progressive_runtime.Dispatch(_state->progressive_runtime,
                                         &progressive_args,
                                         command_buffer);

    command_buffer.End();

    VkPipelineStageFlags wait_dst_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    command_buffer.Submit(nullptr, &_acquire_semaphore, &wait_dst_stage, 1u,
                          &_state->last_submit);

    _state->progressive_runtime.PostSubmit(_state->progressive_runtime, _state->last_submit);

    _vulkan->ScheduleFenceDestroy(_state->last_submit);
    _state->last_submit = VK_NULL_HANDLE;

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

    if (_state->field_desc)
    {
        voxel_field_runtime::FieldDescriptor& descriptor = *_state->field_desc;
        descriptor.upload.wait();

        for (auto upload : *descriptor.pending_uploads)
            _vulkan->CommitStaging(*upload);
        descriptor.pending_uploads->clear();

        _vulkan->DestroyBuffer(descriptor.residency_buffer);
        _vulkan->DestroyBuffer(descriptor.residency_staging);

        for (vktk::Texture& texture : descriptor.masks)
            _vulkan->DestroyTexture(texture);
        for (vktk::Texture& texture : descriptor.offsets)
            _vulkan->DestroyTexture(texture);
        for (vktk::Texture& texture : descriptor.payloads)
            _vulkan->DestroyTexture(texture);
    }

    _state->progressive_runtime.Destroy(_state->progressive_runtime);

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

#if 0
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
#endif

#if 0
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
#endif

#if 0
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
#endif

#if 0
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
    output.vulkan = _vulkan;
    output.registry = &_state->core_registry;
    output.state = _state;
    output.payload = new SparsePipelinePayload{};
    output.graph = SparseRegionRenderSystem(_state->core_registry);

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
#endif

std::unordered_map<std::string, numtk::vec3<uint8_t>> const& BlockColorMap()
{
#include "mcblocks.hh"
    return kBlockColors;
}

}
