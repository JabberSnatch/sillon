#pragma once

#include <cassert>
#include <cstdint>
#include <functional>
#include <memory>
#include <tuple>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

#include <vulkan/vulkan.h>

#include "toolkits/numtk.hh"
#include "toolkits/dstk.hh"

#ifdef NDEBUG

#define CHECKCALL(func, ...)                                            \
    {                                                                   \
        VkResult const vkr = func(__VA_ARGS__);                         \
        if (vkr != VK_SUCCESS)                                          \
            std::cout << #func " " << vktk::VkResultToStr(vkr) << std::endl; \
    }

#else

#define CHECKCALL(func, ...)                                            \
    {                                                                   \
        VkResult const vkr = func(__VA_ARGS__);                         \
        if (vkr != VK_SUCCESS)                                          \
        {                                                               \
            std::cout << #func " " << vktk::VkResultToStr(vkr) << std::endl; \
            assert(false);                                              \
        }                                                               \
    }

#endif

#define GetInstanceProcAddress(proc, inst, table)                       \
    table.proc = (PFN_vk##proc)vkGetInstanceProcAddr(inst, "vk" #proc);

#define GetDeviceProcAddress(proc, dev, table)                          \
    table.proc = (PFN_vk##proc)vkGetDeviceProcAddr(dev, "vk" #proc);


#define kRequiredInstanceProcs(x)               \
    x(GetPhysicalDeviceSurfaceCapabilitiesKHR)  \
    x(GetPhysicalDeviceSurfaceFormatsKHR)       \
    x(GetPhysicalDeviceSurfaceSupportKHR)       \

#define kRequiredDeviceProcs(x)                 \
    x(CreateSwapchainKHR)                       \
    x(DestroySwapchainKHR)                      \
    x(GetSwapchainImagesKHR)                    \
    x(AcquireNextImageKHR)                      \
    x(QueuePresentKHR)                          \

#define GetInstanceProcAddress_XMacro(proc) GetInstanceProcAddress(proc, INSTANCE, TABLE)
#define GetDeviceProcAddress_XMacro(proc) GetDeviceProcAddress(proc, DEVICE, TABLE)

namespace vktk
{

char const* VkResultToStr(VkResult _vkResult);
uint32_t VkFormatBytesPerUnit(VkFormat _format);
void* PNextLookup(VkBaseOutStructure* _out_struct, VkStructureType _type);

struct FPTable
{
#define AsFPTableEntry(proc) PFN_vk##proc proc = nullptr;
    kRequiredInstanceProcs(AsFPTableEntry)

    kRequiredDeviceProcs(AsFPTableEntry)
#undef AsFPTableEntry
};

struct Context;

struct Allocation
{
    uint32_t memory_type;
    VkDeviceMemory memory;
    VkDeviceSize offset;
    VkDeviceSize size;
    void* map;
};

struct Allocator
{
    static constexpr VkDeviceSize kBlockSize = 128;
    static constexpr VkDeviceSize kChunkBlockCount = 1024 * 1024;
    static constexpr VkDeviceSize kChunkSize = kBlockSize * kChunkBlockCount;

    using BlockRange = std::pair<uint64_t, uint64_t>;

    struct ChunkStorage
    {
        uint32_t memory_type;
        std::vector<VkDeviceMemory> chunks; // 128 MB
        std::vector<BlockRange> free_list;
        std::vector<void*> maps;
    };
    VkDevice device;
    VkPhysicalDeviceMemoryProperties memory_properties;
    std::vector<ChunkStorage> heaps = {};
    uint64_t allocated_memory = 0ull;

    Allocation Allocate(VkMemoryPropertyFlags _memory_flags,
                        uint32_t _size);
    Allocation Allocate(VkMemoryPropertyFlags _memory_flags,
                        uint32_t _size,
                        VkDeviceSize _alignment);

    void Free(Allocation const& _alloc);

    uint32_t SelectMemoryType(VkMemoryPropertyFlags _flags, uint32_t _required_size);
    void AllocateChunk(ChunkStorage& _storage);

    void Release();
};

struct Buffer {
    VkBuffer buffer;
    Allocation alloc;

    operator VkBuffer() { return buffer; }
    operator VkBuffer() const { return buffer; }
};

struct Texture {
    VkImage image;
    Allocation alloc;
    VkImageType image_type;
    VkFormat format;
    VkImageView view;
};

struct Swapchain {
    VkExtent2D size = { 0, 0 };
    Context* vulkan;

    VkFormat format;
    VkRenderPass render_pass;
    VkSwapchainKHR handle = VK_NULL_HANDLE;
    std::vector<VkImage> images;
    std::vector<VkImageView> views;
    std::vector<VkFramebuffer> framebuffers;
    std::vector<Texture> depth_buffers;

    uint32_t index;

    VkFramebuffer AcquireNext(VkSemaphore _signal);
    void Release();
};

struct DescriptorPool {
    VkDescriptorPool pool;
    VkDescriptorSetLayout layout;
};

struct DynamicDescriptorPool {
    static constexpr uint32_t kPoolSize = 128;

    struct vktk::Context* context;
    VkDescriptorSetLayout layout;
    std::vector<VkDescriptorPool> pools;
    std::vector<std::unordered_set<VkDescriptorSet>> sets;
    std::vector<uint32_t> set_counts;
    uint32_t first_free_pool = ~0u;
    uint32_t total_set_count = 0;

    std::vector<VkDescriptorPoolSize> sizes;

    VkDescriptorSet AllocateDescriptorSet(void const* _pNext = nullptr);
    void FreeDescriptorSet(VkDescriptorSet _set);
};

struct BufferTransition {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkAccessFlags src_access = VK_ACCESS_NONE;
    VkAccessFlags dst_access = VK_ACCESS_NONE;
};

struct ImageTransition {
    VkImage image = VK_NULL_HANDLE;
    VkAccessFlags src_access = VK_ACCESS_NONE;
    VkAccessFlags dst_access = VK_ACCESS_NONE;
    VkImageLayout old_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageLayout new_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageAspectFlags aspect_flags = VK_IMAGE_ASPECT_NONE;
};

struct Queue {
    struct vktk::Context* context;
    uint32_t family_index;
    VkQueue queue;
    operator VkQueue() { return queue; }
    VkCommandPool command_pool;

    void Submit(VkCommandBuffer const* _command_buffers,
                uint32_t _command_buffer_count,
                VkSemaphore* _signal_semaphore = nullptr,
                VkSemaphore const* _wait_semaphores = nullptr,
                VkPipelineStageFlags const* _wait_stages = nullptr,
                uint32_t _wait_count = 0u,
                VkFence* _signal_fence = nullptr) const;
};

struct FencePool {
    using Handle = dstk::FreeList::Index;
    VkFence AllocateFence(Context& context_);
    void ResetFence(Context const& context_, VkFence fence_);
    std::vector<VkFence> fences;
    dstk::FreeList free_list;
};

struct CommandBuffer {
    Queue const& queue;
    VkCommandBuffer handle;
    std::vector<Buffer> pending_destroys = {};
    operator VkCommandBuffer() { return handle; }

    struct SubmitInfo {
        VkSemaphore* signal_semaphore = nullptr;
        VkSemaphore const* wait_semaphores = nullptr;
        VkPipelineStageFlags const* wait_stages = nullptr;
        uint32_t wait_count = 0u;
        VkFence* signal_fence;
    };

    void Submit(VkSemaphore* _signal_semaphore = nullptr,
                VkSemaphore const* _wait_semaphores = nullptr,
                VkPipelineStageFlags const* _wait_stages = nullptr,
                uint32_t _wait_count = 0u,
                VkFence* _signal_fence = nullptr);
    void Submit(SubmitInfo const& _submit_info) {
        Submit(_submit_info.signal_semaphore,
               _submit_info.wait_semaphores,
               _submit_info.wait_stages,
               _submit_info.wait_count,
               _submit_info.signal_fence);
    }

    void Begin();

    void BufferCopy(VkBuffer _dst, uint64_t _dst_offset,
                    VkBuffer _src, uint64_t _src_offset, uint64_t _size);

#if 0
    void BufferCopy(Buffer const& _dst, uint64_t _dst_offset,
                    Buffer const& _src, uint64_t _src_offset, uint64_t _size);
#endif

    void UpdateTexture(Texture _texture, VkOffset3D _offset, VkExtent3D _extent,
                       std::function<void(void*)> const& _upload_callback);

    void UpdateTexture(Texture _texture, VkOffset3D _offset, VkExtent3D _extent,
                       uint32_t _mip_level,
                       std::function<void(void*)> const& _upload_callback);

    void BufferTransitions(VkPipelineStageFlags _src_stage,
                           VkPipelineStageFlags _dst_stage,
                           std::vector<BufferTransition> const& _transitions);

    void BufferTransitions(CommandBuffer const& _dst,
                           VkPipelineStageFlags _src_stage,
                           VkPipelineStageFlags _dst_stage,
                           std::vector<BufferTransition> const& _transitions);

    void ImageTransitions(VkPipelineStageFlags _src_stage,
                          VkPipelineStageFlags _dst_stage,
                          std::vector<ImageTransition> const& _transitions);

    void BeginRenderPass(VkExtent2D _size,
                         VkFramebuffer _target,
                         VkRenderPass _render_pass,
                         std::vector<VkClearValue> const& _clear_values);

    void BeginRenderPass(numtk::bounds2i const& _viewport,
                         VkFramebuffer _target,
                         VkRenderPass _render_pass,
                         std::vector<VkClearValue> const& _clear_values);

    void End();
};

enum StateOverrideTag {
    CullMode,
    DepthTestEnable,
    DepthWriteEnable,
};

struct Context
{
    Context(uint64_t _hinstance, uint64_t _hwindow);
    ~Context();
    Context(Context const&) = delete;
    Context(Context&&) = delete;
    Context& operator=(Context const&) = delete;
    Context& operator=(Context&&) = delete;

    VkShaderModule CompileShader_GLSL(VkShaderStageFlagBits _shaderStage,
                                      char const* _shaderName,
                                      std::vector<std::string> const& _code);

    VkShaderModule CompileShader_GLSL(VkShaderStageFlagBits _shaderStage,
                                      char const* _shaderName,
                                      char const* _code,
                                      size_t _codeSize);

    CommandBuffer CreateCommandBuffer(Queue const& queue);

    struct RenderPassAttachment {
        VkFormat format;
        VkImageLayout initialLayout;
        VkImageLayout finalLayout;
    };
    VkRenderPass CreateRenderPass(std::vector<RenderPassAttachment> const& _attachments);
    VkRenderPass CreateRenderPass(std::vector<RenderPassAttachment> const& _attachments,
                                  RenderPassAttachment const& _depth_attachment);
    VkRenderPass CreateRenderPass(std::vector<RenderPassAttachment> const& _attachments,
                                  RenderPassAttachment const* _depth_attachment);

    VkFramebuffer CreateFramebuffer(VkRenderPass _render_pass,
                                    numtk::vec2u _size,
                                    std::vector<VkImageView> const& _attachments);

    struct PipelineStage {
        VkShaderModule module;
        VkShaderStageFlagBits stage;
    };
    VkPipeline CreatePipeline(VkPipelineLayout _layout,
                              VkRenderPass _renderPass,
                              std::vector<PipelineStage> const& _stages);

    struct VertexInput {
        uint32_t location;
        uint32_t binding;
        VkVertexInputRate rate;
        VkFormat format;
        uint32_t offset;
        uint32_t stride;
    };
    VkPipeline CreatePipeline(VkPipelineLayout _layout,
                              VkRenderPass _renderPass,
                              std::vector<PipelineStage> const& _stages,
                              VkPrimitiveTopology _topology,
                              std::vector<VertexInput> const& _input);

    struct StateOverride {
        StateOverrideTag tag;
        union DataUnion {
            VkCullModeFlags cull_mode;
            VkBool32 depth_test_enable;
            VkBool32 depth_write_enable;
        } state;
    };

    VkPipeline CreatePipeline(VkPipelineLayout _layout,
                              VkRenderPass _renderPass,
                              std::vector<PipelineStage> const& _stages,
                              std::vector<StateOverride> const& _states,
                              VkPrimitiveTopology _topology,
                              std::vector<VertexInput> const& _input);

    Swapchain CreateSwapchain(uint32_t const _size[2]);

    struct DescriptorSetBindingPoint {
        VkDescriptorType type;
        VkDescriptorBindingFlags flags;
        VkShaderStageFlags shader_stages;
        uint32_t count;
    };
    DescriptorPool CreateDescriptorPool(
        uint32_t _max_sets,
        std::vector<DescriptorSetBindingPoint> const& _bindings);
    void DestroyDescriptorPool(DescriptorPool const& _pool) const;

    DynamicDescriptorPool CreateDynamicDescriptorPool(std::vector<DescriptorSetBindingPoint> const& _bindings);
    void DestroyDynamicDescriptorPool(DynamicDescriptorPool const& _pool) const;

    Buffer CreateBuffer(uint32_t _size,
                        VkBufferUsageFlags _usage,
                        VkMemoryPropertyFlags _memory_flags =
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                        | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    Buffer CreateBuffer(uint32_t _size,
                        VkBufferUsageFlags _usage,
                        std::function<void(void*)> const& _upload_callback,
                        VkMemoryPropertyFlags _memory_flags =
                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    void DestroyBuffer(Buffer const& _buffer);

    Texture CreateTexture(uint32_t _width, uint32_t _height, uint32_t _depth,
                          VkImageType _type, VkFormat _format, VkImageUsageFlags _usage);

    Texture CreateTexture(uint32_t _width, uint32_t _height, uint32_t _depth,
                          uint32_t _mip_count,
                          VkImageType _type, VkFormat _format, VkImageUsageFlags _usage);

    Texture CreateTexture(uint32_t _width, uint32_t _height, uint32_t _depth,
                          VkImageType _type, VkFormat _format, VkImageUsageFlags _usage,
                          std::function<void(void*)> const& _upload_callback);
    void DestroyTexture(Texture const& _texture);

    VkImageView CreateImageView(Texture const& _texture) const;

    FPTable fp = {};

    VkInstance instance = {};
    VkPhysicalDevice physical_device = {};
    VkPhysicalDeviceMemoryProperties memory_properties = {};

    uint32_t SelectMemoryType(VkMemoryPropertyFlags _flags, uint32_t _required_size);
    VkDeviceMemory MemAlloc(VkMemoryPropertyFlags _flags, uint32_t _size);

    VkSurfaceKHR surface = {};
    VkDevice device = {};
    Queue present_queue = {};
    Queue copy_queue = {};

    Allocator allocator = {};

    FencePool fence_pool = {};
    uint32_t fence_count = 0;

    using SynchronisedRoutine = std::function<void(Context&)>;
    void ScheduleRoutine(VkFence _fence, SynchronisedRoutine _routine);
    void ScheduleFenceDestroy(VkFence _fence);
    void TryScheduledRoutines();
    void WaitScheduledRoutines();
    std::unordered_map<VkFence, std::vector<SynchronisedRoutine>> pending_routines = {};
    std::unordered_set<VkFence> pending_fences = {};
};

} // namespace vktk

namespace vkrg
{

namespace layout
{

struct ShaderStage {
    VkShaderStageFlagBits stage;
    std::vector<std::string> source;
};

struct StageBinding {
    VkShaderStageFlags stages;
    VkDescriptorType type;
    VkDescriptorBindingFlags flags;
    uint32_t count;
};

struct StageBindingGroup {
    std::vector<StageBinding> bindings;
};

struct PushConstantRange {
    VkShaderStageFlags stages;
    uint32_t offset;
    uint32_t size;
};

struct GraphicsStage {
    std::vector<vktk::Context::StateOverride> state_overrides;

    // geometry attachments
    VkPrimitiveTopology topology;
    std::vector<vktk::Context::VertexInput> input_layout;
};

struct DispatchStage {
    std::string name;
    std::vector<ShaderStage> shaders;
    uint32_t invocation_count;
    std::vector<StageBindingGroup> binding_groups;
    std::vector<PushConstantRange> push_constants;
    GraphicsStage graphics;
};

enum DispatchMode {
    DispatchMode_Graphics,
    DispatchMode_Compute,
};

union ClearValue {
    numtk::vec4f color;
    struct {
        float depth;
        uint32_t stencil;
    } depth_stencil;
};

struct RenderAttachment {
    VkImageType type;
    VkFormat format;
    ClearValue clear_value;
};

struct GraphicsNode {
    std::vector<RenderAttachment> attachments;
};

struct DispatchNode {
    std::string name;
    DispatchMode mode;
    std::vector<DispatchStage> sequence;
    GraphicsNode graphics;
};

struct ShaderLocation {
    uint32_t group_index;
    uint32_t binding_index;
};

struct FramebufferLocation {
    uint32_t attachment_index;
};

struct PushConstantsLocation {
    uint32_t range_index;
};

struct VertexInputLocation {
    uint32_t binding_index;
};

enum BindingType {
    BindingType_Shader,
    BindingType_Framebuffer,
    BindingType_PushConstantRange,
    BindingType_VertexInput,
    BindingType_Present,
};

struct BindingPoint {
    DispatchNode const* node;
    uint32_t stage_index;
    BindingType type;
    union {
        ShaderLocation shader;
        FramebufferLocation framebuffer;
        PushConstantsLocation push_constants;
        VertexInputLocation vertex_input;
    };
};

enum EdgeType {
    EdgeType_Image,
    EdgeType_Buffer,
};

struct GraphEdge {
    EdgeType type;
    BindingPoint source;
    BindingPoint target;
};

struct BindingPointSync {
    VkPipelineStageFlags stage;
    VkAccessFlags access;
    VkImageLayout layout;
};

struct DispatchGraph {
    std::vector<DispatchNode> nodes;
    std::vector<GraphEdge> edges;

    std::unordered_map<DispatchNode const*, std::vector<uint32_t>> edges_in;
    std::unordered_map<DispatchNode const*, std::vector<uint32_t>> edges_out;

    DispatchNode const* operator[](char const* node_id) const;

    void InsertEdge(GraphEdge const& edge);
    std::vector<vktk::Context::RenderPassAttachment> RenderPassDescription(
        uint32_t _node_index,
        VkFormat const* _swapchain_format = nullptr) const;

    static BindingPointSync BindingPointSyncRequirements(EdgeType type, BindingPoint const& binding_point);
};

namespace _ {

struct DispatchStage {
    std::vector<ShaderStage> shaders;
    std::vector<StageBindingGroup> binding_groups;
    std::vector<PushConstantRange> push_constants;
    GraphicsStage graphics;
};

struct DispatchPass {
    DispatchMode mode;
    GraphicsNode graphics;
};

} // namespace _

namespace _
{

enum NodeTag {
    NodeTag_None,
    NodeTag_Output,
    NodeTag_Input,
    NodeTag_Present
};

enum BindingType {
    BindingType_None,
    BindingType_Framebuffer,
    BindingType_Shader,
};

struct BindingPoint {
    BindingType type = BindingType_None;
    union {
        uint32_t attachment_index;
        struct {
            uint32_t group_index;
            uint32_t binding_index;
        } shader;
    };
};

struct EdgeBindings {
    BindingPoint source;
    BindingPoint target;
};

#if 1
struct DispatchGraph
{
    DispatchGraph(dstk::Registry* registry_ = nullptr)
        : registry{ registry_ }
    {}
    DispatchGraph(DispatchGraph const&) = default;
    DispatchGraph& operator=(DispatchGraph const&) = default;
    DispatchGraph(DispatchGraph&&) = default;
    DispatchGraph& operator=(DispatchGraph&&) = default;

    struct EdgeDesc {
        dstk::Registry::Node source;
        dstk::Registry::Node target;
    };

    DispatchGraph& MakeSubgraph(std::string const& parent_node) {
        dstk::Registry::Node node = this->operator[](parent_node);
        DispatchGraph output{ registry };
        output.parent_node = node;
        registry->BindComponent(node, output);
        return *registry->ComponentLookup<DispatchGraph>(node);
    }

    dstk::Registry::Node MakeNode(std::string const& name = "") {
        dstk::Registry::Node node = registry->MakeNode();
        if (name != "" && !name_lookup.contains(name))
            name_lookup.emplace(node, name);
        return node;
    }

    dstk::Registry::Node MakeEdge(std::string const& source, std::string const& target) {
        dstk::Registry::Node source_node = name_lookup[source];
        dstk::Registry::Node target_node = name_lookup[target];

        dstk::Registry::Node edge = registry->handle_pool.Reserve();
        edges_in[target_node].push_back(edge);
        edges_out[source_node].push_back(edge);
        registry->BindComponent(edge, EdgeDesc{ source_node, target_node });
        return edge;
    }

    DispatchGraph const& Subgraph(std::string const& name) const {
        return *registry->ComponentLookup<DispatchGraph>((*this)[name]);
    }

    std::vector<dstk::Registry::Node> Inputs() const {
        std::vector<dstk::Registry::Node> result = {};
        for (auto const& pair : name_lookup.pairs)
        {
            if (edges_in.count(pair.first) == 0
                || edges_in.at(pair.first).empty())
                result.push_back(pair.first);
        }
        return result;
    }

    std::vector<dstk::Registry::Node> Outputs() const {
        std::vector<dstk::Registry::Node> result = {};
        for (auto const& pair : name_lookup.pairs)
        {
            if (edges_out.count(pair.first) == 0
                || edges_out.at(pair.first).empty())
                result.push_back(pair.first);
        }
        return result;
    }

    dstk::Registry::Node operator[](std::string const& name) {
        dstk::Registry::Node node = ~(dstk::Registry::Node)0;
        if (!name_lookup.contains(name)) {
            node = registry->MakeNode();
            name_lookup.emplace(node, name);
        }
        else
            node = name_lookup[name];
        return node;
    }

    dstk::Registry::Node operator[](std::string const& name) const {
        if (!name_lookup.contains(name))
            return ~(dstk::Registry::Node)0;
        return name_lookup[name];
    }

    void DepthFirstTraversal(std::function<void(std::vector<vkrg::layout::_::DispatchGraph const*> const&, dstk::Registry::Node)> const& callback) const {
        std::vector<dstk::Registry::Node> node_stack = Inputs();
        std::vector<vkrg::layout::_::DispatchGraph const*> graph_stack{ this };

        while (!node_stack.empty())
        {
            dstk::Registry::Node current_node = node_stack.back();
            vkrg::layout::_::DispatchGraph const* current_graph = graph_stack.back();
            vkrg::layout::_::DispatchGraph const* dispatch_graph = registry->ComponentLookup<vkrg::layout::_::DispatchGraph>(current_node);

            while (!current_graph->name_lookup.contains(current_node))
            {
                graph_stack.pop_back();
                current_graph = graph_stack.back();
                dispatch_graph = nullptr;
            }

            callback(graph_stack, current_node);

            if (dispatch_graph)
            {
                std::vector<dstk::Registry::Node> graph_inputs = dispatch_graph->Inputs();
                node_stack.insert(node_stack.end(), graph_inputs.begin(), graph_inputs.end());
                graph_stack.push_back(dispatch_graph);
            }
            else
            {
                node_stack.pop_back();
                if (current_graph->edges_out.count(current_node))
                {
                    for (dstk::Registry::Node edge : current_graph->edges_out.at(current_node))
                    {
                        vkrg::layout::_::DispatchGraph::EdgeDesc const* edge_desc =
                            registry->ComponentLookup<vkrg::layout::_::DispatchGraph::EdgeDesc>(edge);
                        node_stack.push_back(edge_desc->target);
                    }
                }
            }
        }
    }

    dstk::Registry* registry;

    dstk::Registry::Node parent_node = dstk::FreeList::kInvalidIndex;
    std::unordered_map<dstk::Registry::Node, std::vector<dstk::Registry::Node>> edges_in;
    std::unordered_map<dstk::Registry::Node, std::vector<dstk::Registry::Node>> edges_out;
    dstk::BijectiveMap<dstk::Registry::Node, std::string> name_lookup;
};

#else

struct DispatchGraph
{
    using Handle = dstk::FreeList::Index;

    struct Node {
        Handle handle;
        vkrg::layout::_::NodeTag node_tag = vkrg::layout::_::NodeTag_None;
        std::unique_ptr<struct DispatchGraph> subgraph;
        std::unique_ptr<vkrg::layout::_::DispatchPass> pass_desc;
        std::unique_ptr<vkrg::layout::DispatchStage> stage_desc;
    };

    struct Edge {
        Handle source;
        Handle target;
        vkrg::layout::_::BindingPoint source_point;
        vkrg::layout::_::BindingPoint target_point;
    };

    Edge& MakeEdge(std::string const& source, std::string const& target) {
        Handle source_handle = name_lookup[source];
        Handle target_handle = name_lookup[target];

        Handle handle = handle_pool.Reserve();
        edges_in[target_handle].push_back(handle);
        edges_out[source_handle].push_back(handle);
        Edge& output = edges[handle];
        output.source = source_handle;
        output.target = target_handle;
        return output;
    }

    Node& operator[](std::string const& name) {
        Handle handle = ~0ull;
        if (name_lookup.count(name) == 0) {
            handle = handle_pool.Reserve();
            name_lookup.emplace(name, handle);
            Node& output = nodes[handle];
            output.handle = handle;
            return output;
        }
        else
            return nodes[name_lookup.at(name)];
    }

    std::unordered_map<std::string, Handle> name_lookup;
    std::unordered_map<Handle, std::vector<Handle>> edges_in;
    std::unordered_map<Handle, std::vector<Handle>> edges_out;
    std::unordered_map<Handle, Node> nodes;
    std::unordered_map<Handle, Edge> edges;
    dstk::FreeList handle_pool;
};

#endif

} // namespace _

} // namespace layout

namespace bind
{

struct ShaderResource {
    uint32_t descriptor_count;
    union {
        VkDescriptorImageInfo const* images;
        VkDescriptorBufferInfo const* buffers;
    };
};

struct VertexInput {
    VkBuffer buffer;
    VkDeviceSize offset;
};

struct IndexInput {
    VkBuffer buffer;
    VkDeviceSize offset;
};

enum ResourceType {
    BufferResource,
    ImageResource
};

struct ResourceBarrier {
    ResourceType type;
    VkPipelineStageFlags stage;
    VkAccessFlags access;
    union {
        VkBuffer buffer;
        VkImage image;
    };
};

struct AccessInfo {
    VkPipelineStageFlags stage;
    VkAccessFlags access;
};

enum DispatchCommand {
    DispatchCommand_Draw,
    DispatchCommand_DrawIndexed,
};

struct GraphicsCommand {
    DispatchCommand command;
    uint32_t vertex_count;
    uint32_t instance_count;
};

struct FramebufferArea {
    uint32_t framebuffer_index;
    numtk::bounds2u scissor;
    numtk::bounds2f viewport;
};

struct DispatchInvocation {
    GraphicsCommand graphics;
    std::vector<std::unique_ptr<uint8_t[]>> push_constants;
    std::vector<VertexInput> vertex_inputs;
    IndexInput index_input;

    DispatchInvocation() = default;
    DispatchInvocation(DispatchInvocation&&) = default;
};

struct DispatchStage {
    std::vector<DispatchInvocation> invocations;
    std::vector<ResourceBarrier> barriers;
    std::unordered_map<VkBuffer, std::pair<AccessInfo, AccessInfo>> buffer_barriers;

    DispatchStage() = default;
    DispatchStage(DispatchStage&&) = default;
};

struct DispatchNode {
    FramebufferArea area_binding;
    std::vector<VkFramebuffer> framebuffers;
    std::vector<DispatchStage> stages;
};

struct PNextEntry {
    uint32_t binding_group_index;
    void* pnext;
};

using PNextMap = std::unordered_map<std::string, std::vector<PNextEntry>>;

namespace _ {

struct ShaderResources {
    std::vector<VkDescriptorImageInfo> images;
    std::vector<VkDescriptorBufferInfo> buffers;
};

struct ShaderBinding {
    ::vkrg::layout::_::BindingPoint target;
    ShaderResources resources;
};

} // namespace _

} // namespace bind

namespace runtime
{

struct StageBindingGroupStatic {
    //vktk::DescriptorPool pool;
    vktk::DynamicDescriptorPool pool;
};

struct DispatchStageStatic {
    VkPipelineLayout pipeline_layout;
    std::vector<StageBindingGroupStatic> binding_groups;
};

struct DispatchNodeStatic {
    std::vector<DispatchStageStatic> stages;
};

struct GraphicsNodeFramebuffer {
    VkRenderPass render_pass;
};

struct DispatchNodeFramebuffer {
    std::vector<VkPipeline> stages;
    GraphicsNodeFramebuffer graphics;
};

struct StageBindingGroupDynamic {
    std::vector<VkDescriptorSet> sets;
};

struct DispatchStageDynamic {
    uint32_t invocation_count;
    std::vector<StageBindingGroupDynamic> binding_groups;
};

struct DispatchNodeDynamic {
    std::vector<DispatchStageDynamic> stages;
};

struct DispatchContext {
    vktk::Context* vulkan;
    //layout::DispatchGraph const* graph;

    std::unique_ptr<dstk::Registry> registry{ new dstk::Registry{} };
    vkrg::layout::_::DispatchGraph _graph{ registry.get() };

    std::vector<DispatchNodeStatic> nodes_static;
    std::vector<DispatchNodeFramebuffer> nodes_framebuffer;
    std::vector<DispatchNodeDynamic> nodes_dynamic;
    std::vector<bind::DispatchNode> nodes_bindings;

    void ClearStaticRuntime();
    void ClearFramebufferRuntime();
    void ClearDynamicRuntime();

    void GenerateStaticRuntime();
    void GenerateFramebufferRuntime(VkFormat const* _swapchain_format = nullptr);
    void ResetDynamicRuntime(bind::PNextMap const& _pNext_map = {});
    void Dispatch(vktk::CommandBuffer& _command_buffer) const;

    std::vector<vktk::Context::RenderPassAttachment> RenderPassDescription(
        dstk::Registry::Node _node,
        VkFormat const* _swapchain_format = nullptr) const;

    vkrg::layout::BindingPointSync BindingPointSyncRequirements(
        dstk::Registry::Node _node,
        vkrg::layout::EdgeType _type,
        vkrg::layout::_::BindingPoint const& _binding_point) const;

    void UpdateDynamicInvocationCount(layout::DispatchNode const* _node,
                                      uint32_t _stage_index,
                                      uint32_t _invocation_count);

    void ReserveInvocations(layout::DispatchNode const* _node,
                            uint32_t _stage_index,
                            uint32_t _invocation_count,
                            bind::PNextMap const& _pNext_map = {});

    void BindBarrier(layout::DispatchNode const* _node,
                     uint32_t _stage_index,
                     bind::ResourceBarrier const& _barrier);
    void ClearStageBarriers(layout::DispatchNode const* _node,
                            uint32_t _stage_index);

    void BindSwapchain(dstk::Registry::Node _node,
                       vktk::Swapchain const& _swapchain);
    void BindAttachments(layout::DispatchNode const* _node,
                         numtk::vec2u const& _size,
                         std::vector<VkImageView> const& _attachments);
    void BindFramebufferArea(layout::DispatchNode const* _node,
                             bind::FramebufferArea const& _area);
    void BindGraphicsCommand(layout::DispatchNode const* _node,
                             uint32_t _stage_index,
                             uint32_t _invoc_index,
                             bind::GraphicsCommand const& _command);

    void BindVertexInput(layout::BindingPoint const& _location,
                         uint32_t _invoc_index,
                         bind::VertexInput const& _vertex_input);
    void BindIndexInput(layout::DispatchNode const* _node,
                        uint32_t _stage_index,
                        uint32_t _invoc_index,
                        bind::IndexInput const& _index_input);
    void* BindPushConstants(layout::BindingPoint const& _location,
                            uint32_t _invoc_index);
    VkWriteDescriptorSet BindShaderResource(layout::BindingPoint const& _location,
                                            uint32_t _invoc_index,
                                            bind::ShaderResource const& _resources);
};

} // namespace runtime

} // namespace vkrg
