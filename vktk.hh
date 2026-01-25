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
        if (vkr < VK_SUCCESS)                                           \
            std::cout << #func " " << vktk::VkResultToStr(vkr) << std::endl; \
    }

#else

#define CHECKCALL(func, ...)                                            \
    {                                                                   \
        VkResult const vkr = func(__VA_ARGS__);                         \
        if (vkr < VK_SUCCESS)                                           \
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

    struct BlendState {
        VkBool32 blend_enable;
        VkBlendFactor src_blend_factor;
        VkBlendFactor dst_blend_factor;
        VkBlendOp blend_op;
    };

    VkPipeline CreatePipeline(VkPipelineLayout _layout,
                              VkRenderPass _renderPass,
                              std::vector<PipelineStage> const& _stages,
                              std::vector<StateOverride> const& _states,
                              std::vector<BlendState> const& _blend,
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

enum DispatchMode {
    DispatchMode_Graphics,
    DispatchMode_Compute,
};

struct GraphicsNode {
    std::vector<RenderAttachment> attachments;
};

enum EdgeType {
    EdgeType_Image,
    EdgeType_Buffer,
};

struct BindingPointSync {
    VkPipelineStageFlags stage;
    VkAccessFlags access;
    VkImageLayout layout;
};

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
    std::vector<vktk::Context::BlendState> blend_states;
};

struct DispatchStage {
    std::string name;
    std::vector<ShaderStage> shaders;
    uint32_t invocation_count;
    std::vector<StageBindingGroup> binding_groups;
    std::vector<PushConstantRange> push_constants;
    GraphicsStage graphics;
};

struct DispatchPass {
    DispatchMode mode;
    GraphicsNode graphics;
};

enum NodeTag {
    NodeTag_None,
    NodeTag_Output,
    NodeTag_Input,
    NodeTag_Present,
    NodeTag_BlitImage,
};

enum BindingType {
    BindingType_None,
    BindingType_Framebuffer,
    BindingType_Shader,
    BindingType_PushConstantRange,
    BindingType_VertexInput,
    BindingType_BlitImage,
};

enum BlitImageSlot {
    BlitImageSlot_Source,
    BlitImageSlot_Sink,
};

struct BindingPoint {
    BindingType type = BindingType_None;
    dstk::Registry::Node node_override = dstk::FreeList::kInvalidIndex;
    union {
        uint32_t attachment_index;
        uint32_t push_constant_range;
        uint32_t vertex_input_index;
        struct {
            uint32_t group_index;
            uint32_t binding_index;
        } shader;
        BlitImageSlot blit_slot;
    };
};

struct EdgeBindings {
    EdgeType type;
    BindingPoint source;
    BindingPoint sink;
};

using InvocCountMap = std::unordered_map<dstk::Registry::Node, uint32_t>;

struct DispatchGraph
{
    DispatchGraph(dstk::Registry* registry_ = nullptr)
        : data{ nullptr }
    {
        if (!registry_)
            return;

        dstk::Registry::Node data_node = registry_->MakeNode();
        data = &registry_->ComponentRequest<Data>(data_node);
        data->registry = registry_;
        data->data_node = data_node;
    }

    DispatchGraph(DispatchGraph const&) = default;
    DispatchGraph& operator=(DispatchGraph const&) = default;
    DispatchGraph(DispatchGraph&&) = default;
    DispatchGraph& operator=(DispatchGraph&&) = default;

    struct EdgeDesc {
        dstk::Registry::Node source;
        dstk::Registry::Node sink;
    };

    struct NodeView {
        operator dstk::Registry::Node() const { return node; }
        NodeView operator[](std::string const& name) const {
            return (*graph->data->registry->ComponentLookup<DispatchGraph>(node))[name];
        }
        NodeView operator[](char const* name) {
            return this->operator[](std::string{ name });
        }
        DispatchGraph const* graph;
        dstk::Registry::Node node;
    };

    DispatchGraph& MakeSubgraph(std::string const& parent_node) {
        dstk::Registry::Node node = this->operator[](parent_node);
        DispatchGraph output{ data->registry };
        output.data->parent_node = node;
        data->registry->BindComponent(node, output);
        return *data->registry->ComponentLookup<DispatchGraph>(node);
    }

    void BindSubgraph(std::string const& parent_node, DispatchGraph const& graph) {
        dstk::Registry::Node node = this->operator[](parent_node);
        DispatchGraph& output = data->registry->ComponentRequest<DispatchGraph>(node);
        output = graph;
        output.data->parent_node = node;
    }

    dstk::Registry::Node MakeNode(std::string const& name = "") {
        dstk::Registry::Node node = data->registry->MakeNode();
        if (name != "" && !data->name_lookup.contains(name))
            data->name_lookup.emplace(node, name);
        data->registry->BindComponent(node, data);
        return node;
    }

    dstk::Registry::Node MakeEdge(std::string const& source, std::string const& sink) {
        dstk::Registry::Node source_node = data->name_lookup[source];
        dstk::Registry::Node sink_node = data->name_lookup[sink];

        dstk::Registry::Node edge = data->registry->handle_pool.Reserve();
        data->edges_in[sink_node].push_back(edge);
        data->edges_out[source_node].push_back(edge);
        data->registry->BindComponent(edge, EdgeDesc{ source_node, sink_node });
        return edge;
    }

    DispatchGraph const& Subgraph(std::string const& name) const {
        return *data->registry->ComponentLookup<DispatchGraph>((*this)[name]);
    }

    std::vector<dstk::Registry::Node> Inputs() const {
        std::vector<dstk::Registry::Node> result = {};
        for (auto const& pair : data->name_lookup.pairs)
        {
            if (data->edges_in.count(pair.first) == 0
                || data->edges_in.at(pair.first).empty())
                result.push_back(pair.first);
        }
        return result;
    }

    std::vector<dstk::Registry::Node> Outputs() const {
        std::vector<dstk::Registry::Node> result = {};
        for (auto const& pair : data->name_lookup.pairs)
        {
            if (data->edges_out.count(pair.first) == 0
                || data->edges_out.at(pair.first).empty())
                result.push_back(pair.first);
        }
        return result;
    }

    NodeView operator[](char const* name) {
        return this->operator[](std::string{ name });
    }

    NodeView operator[](char const* name) const {
        return this->operator[](std::string{ name });
    }

    NodeView operator[](std::string const& name) {
        dstk::Registry::Node node = ~(dstk::Registry::Node)0;
        if (!data->name_lookup.contains(name))
            node = MakeNode(name);
        else
            node = data->name_lookup[name];
        return NodeView{ this, node };
    }

    NodeView operator[](std::string const& name) const {
        if (!data->name_lookup.contains(name))
            return NodeView{ this, ~(dstk::Registry::Node)0 };
        return NodeView{ this, data->name_lookup[name] };
    }

    using GraphStack = std::vector<vkrg::layout::DispatchGraph const*>;
    using InvocStack = std::vector<uint32_t>;

    using TraversalCallbackType =
        std::function<void(uint32_t,
                           dstk::Registry::Node)>;

    using InvocationCallbackType =
        std::function<void(uint32_t,
                           InvocStack const&,
                           dstk::Registry::Node)>;

    void DepthFirstTraversal(TraversalCallbackType const& callback) const {
        std::vector<dstk::Registry::Node> node_stack = Inputs();
        std::vector<vkrg::layout::DispatchGraph const*> graph_stack{ this };

        uint32_t base_depth = Depth();
        while (!node_stack.empty())
        {
            dstk::Registry::Node current_node = node_stack.back();
            vkrg::layout::DispatchGraph const* current_graph = graph_stack.back();
            vkrg::layout::DispatchGraph const* dispatch_graph = data->registry->ComponentLookup<vkrg::layout::DispatchGraph>(current_node);

            bool skip_callback = false;
            while (!current_graph->data->name_lookup.contains(current_node))
            {
                graph_stack.pop_back();
                current_graph = graph_stack.back();
                dispatch_graph = nullptr;
                skip_callback = true;
            }

            if (!skip_callback)
                callback(graph_stack.size() + base_depth, current_node);

            if (dispatch_graph)
            {
                std::vector<dstk::Registry::Node> graph_inputs = dispatch_graph->Inputs();
                node_stack.insert(node_stack.end(), graph_inputs.begin(), graph_inputs.end());
                graph_stack.push_back(dispatch_graph);
            }
            else
            {
                node_stack.pop_back();
                if (current_graph->data->edges_out.count(current_node))
                {
                    for (dstk::Registry::Node edge : current_graph->data->edges_out.at(current_node))
                    {
                        vkrg::layout::DispatchGraph::EdgeDesc const* edge_desc =
                            data->registry->ComponentLookup<vkrg::layout::DispatchGraph::EdgeDesc>(edge);
                        node_stack.push_back(edge_desc->sink);
                    }
                }
            }
        }
    }

    void DepthFirstInvocation(InvocationCallbackType const& callback) const {

        static auto const impl = [](vkrg::layout::DispatchGraph const* root,
                                    auto&& callback,
                                    auto&& recursion,
                                    uint32_t base_depth,
                                    std::vector<vkrg::layout::DispatchGraph const*> graph_stack,
                                    std::vector<uint32_t> invoc_stack)
            -> void
        {
            vkrg::layout::DispatchGraph const* current_graph = graph_stack.back();
            std::vector<dstk::Registry::Node> node_stack = current_graph->Inputs();

            while (!node_stack.empty())
            {
                dstk::Registry::Node current_node = node_stack.back();
                vkrg::layout::DispatchGraph const* subgraph = root->data->registry->ComponentLookup<vkrg::layout::DispatchGraph>(current_node);

                callback(graph_stack.size() + base_depth, invoc_stack, current_node);

                if (subgraph)
                {
                    uint32_t node_invoc_count =
                        root->data->invoc_count.count(current_node)
                        ? root->data->invoc_count.at(current_node) : 1u;

                    graph_stack.push_back(subgraph);
                    invoc_stack.push_back(0u);
                    for (uint32_t invoc_index = 0u; invoc_index < node_invoc_count; ++invoc_index)
                    {
                        invoc_stack.back() = invoc_index;
                        recursion(root, callback, recursion, base_depth, graph_stack, invoc_stack);
                    }
                    invoc_stack.pop_back();
                    graph_stack.pop_back();
                }

                node_stack.pop_back();
                if (current_graph->data->edges_out.count(current_node))
                {
                    for (dstk::Registry::Node edge : current_graph->data->edges_out.at(current_node))
                    {
                        vkrg::layout::DispatchGraph::EdgeDesc const* edge_desc =
                            root->data->registry->ComponentLookup<vkrg::layout::DispatchGraph::EdgeDesc>(edge);
                        node_stack.push_back(edge_desc->sink);
                    }
                }
            }
        };

        impl(this, callback, impl, Depth(), { this }, { 0u });
    }

    uint32_t Depth() const {
        Data* current_data = data;
        uint32_t depth = 0u;
        while (current_data->parent_node != dstk::FreeList::kInvalidIndex) {
            current_data =
                *current_data->registry->ComponentLookup<Data*>(current_data->parent_node);
            ++depth;
        }
        return depth;
    }

    struct Data {
        dstk::Registry* registry;
        dstk::Registry::Node data_node;
        dstk::Registry::Node parent_node = dstk::FreeList::kInvalidIndex;
        std::unordered_map<dstk::Registry::Node, std::vector<dstk::Registry::Node>> edges_in;
        std::unordered_map<dstk::Registry::Node, std::vector<dstk::Registry::Node>> edges_out;
        dstk::BijectiveMap<dstk::Registry::Node, std::string> name_lookup;
        InvocCountMap invoc_count;
    };

    Data* data;
};

} // namespace layout

namespace bind
{

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

struct VertexInput {
    VkBuffer buffer;
    VkDeviceSize offset;
};

struct IndexInput {
    VkBuffer buffer;
    VkDeviceSize offset;
};

struct PNextEntry {
    uint32_t binding_group_index;
    void* pnext;
};

using PNextMap = std::unordered_map<std::string, std::vector<PNextEntry>>;

struct AccessInfo {
    VkPipelineStageFlags stage;
    VkAccessFlags access;
};

struct VertexBindings {
    std::vector<VertexInput> vertex_inputs;
    IndexInput index_input;
};

struct StageBarriers {
    std::vector<ResourceBarrier> barriers;
    std::unordered_map<VkBuffer, std::pair<AccessInfo, AccessInfo>> buffer_barriers;
};

using PushConstantStorage = std::vector<std::unique_ptr<uint8_t[]>>;

struct ShaderResources {
    std::vector<VkDescriptorImageInfo> images;
    std::vector<VkDescriptorBufferInfo> buffers;
};

struct ShaderBinding {
    ::vkrg::layout::BindingPoint sink;
    ShaderResources resources;
};

struct BlitCommand {
    numtk::bounds3u source_bounds;
    numtk::bounds3u sink_bounds;
    VkImageAspectFlagBits aspect_mask;
    VkFilter filter;
};

} // namespace bind

namespace runtime
{

struct StageBindingGroupStatic {
    vktk::DynamicDescriptorPool pool;
};

struct DispatchStageStatic {
    VkPipelineLayout pipeline_layout;
    std::vector<StageBindingGroupStatic> binding_groups;
};

struct StageBindingGroupDynamic {
    std::vector<VkDescriptorSet> sets;
};

struct DispatchStageDynamic {
    uint32_t invocation_count;
    std::vector<StageBindingGroupDynamic> binding_groups;
    std::vector<uint32_t> invoc_count_stack;
};

struct DispatchContext {

    DispatchContext(dstk::Registry& _registry, vktk::Context& _vulkan,
                    vkrg::layout::DispatchGraph const& _graph)
        : registry{ _registry }
        , vulkan{ _vulkan }
        , graph{ _graph }
    {}

    dstk::Registry& registry;
    vktk::Context& vulkan;
    vkrg::layout::DispatchGraph const& graph;

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
        vkrg::layout::BindingPoint const& _binding_point) const;

    void UpdateDynamicInvocationCount(dstk::Registry::Node _node,
                                      uint32_t _invocation_count);

    void BindBarrier(dstk::Registry::Node _node,
                     bind::ResourceBarrier const& _barrier);
    void ClearStageBarriers(dstk::Registry::Node _node);

    void BindSwapchain(dstk::Registry::Node _node,
                       vktk::Swapchain const& _swapchain);
    void BindAttachments(dstk::Registry::Node _node,
                         numtk::vec2u const& _size,
                         std::vector<VkImageView> const& _attachments);

    void BindFramebufferArea(dstk::Registry::Node _node,
                             bind::FramebufferArea const& _area);

    void BindBlitCommand(dstk::Registry::Node _node,
                         bind::BlitCommand const& _command);

    void BindImage(dstk::Registry::Node _node,
                   layout::BindingPoint const& _location,
                   VkImage _image);

    static uint32_t ComputeBindingIndex(std::vector<uint32_t> const& _invoc_count_stack,
                                        std::vector<uint32_t> const& _invoc_indices);

    void BindGraphicsCommand(dstk::Registry::Node _node,
                             std::vector<uint32_t> const& _invoc_indices,
                             bind::GraphicsCommand const& _command);
    void BindIndexInput(dstk::Registry::Node _node,
                        std::vector<uint32_t> const& _invoc_indices,
                        bind::IndexInput const& _index_input);
    void BindVertexInput(dstk::Registry::Node _node,
                         layout::BindingPoint const& _location,
                         std::vector<uint32_t> const& _invoc_indices,
                         bind::VertexInput const& _vertex_input);
    void* BindPushConstants(dstk::Registry::Node _node,
                            layout::BindingPoint const& _location,
                            std::vector<uint32_t> const& _invoc_indices);
    VkWriteDescriptorSet BindShaderResource(dstk::Registry::Node _node,
                                            layout::BindingPoint const& _location,
                                            std::vector<uint32_t> const& _invoc_indices,
                                            bind::ShaderResources const& _resources);

    void BindGraphicsCommand(dstk::Registry::Node _node,
                             uint32_t _invoc_index,
                             bind::GraphicsCommand const& _command);
    void BindIndexInput(dstk::Registry::Node _node,
                        uint32_t _invoc_index,
                        bind::IndexInput const& _index_input);
    void BindVertexInput(dstk::Registry::Node _node,
                         layout::BindingPoint const& _location,
                         uint32_t _invoc_index,
                         bind::VertexInput const& _vertex_input);
    void* BindPushConstants(dstk::Registry::Node _node,
                            layout::BindingPoint const& _location,
                            uint32_t _invoc_index);
    VkWriteDescriptorSet BindShaderResource(dstk::Registry::Node _node,
                                            layout::BindingPoint const& _location,
                                            uint32_t _invoc_index,
                                            bind::ShaderResources const& _resources);
};

} // namespace runtime

} // namespace vkrg
