#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

#include <vulkan/vulkan.h>

#include "toolkits/numtk.hh"

#define CHECKCALL(func, ...)                                            \
    {                                                                   \
        VkResult const vkr = func(__VA_ARGS__);                         \
        if (vkr != VK_SUCCESS)                                          \
            std::cout << #func " " << vktk::VkResultToStr(vkr) << std::endl; \
    }

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
    };
    VkDevice device;
    VkPhysicalDeviceMemoryProperties memory_properties;
    std::vector<ChunkStorage> heaps = {};

    Allocation Alloc(VkMemoryPropertyFlags _memory_flags,
                     uint32_t _size);
    Allocation Alloc(VkMemoryPropertyFlags _memory_flags,
                     uint32_t _size,
                     VkDeviceSize _alignment);

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
    Context const* vulkan;

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

struct CommandBuffer {
    Queue const& queue;
    VkCommandBuffer handle;
    std::vector<Buffer> pending_destroys = {};
    operator VkCommandBuffer() { return handle; }

    void Submit(VkSemaphore* _signal_semaphore = nullptr,
                VkSemaphore const* _wait_semaphores = nullptr,
                VkPipelineStageFlags const* _wait_stages = nullptr,
                uint32_t _wait_count = 0u,
                VkFence* _signal_fence = nullptr);

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

    Buffer CreateBuffer(uint32_t _size,
                        VkBufferUsageFlags _usage,
                        VkMemoryPropertyFlags _memory_flags =
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                        | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    Buffer CreateBuffer(uint32_t _size,
                        VkBufferUsageFlags _usage,
                        std::function<void(void*)> const& _upload_callback,
                        VkMemoryPropertyFlags _memory_flags =
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                        | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    void DestroyBuffer(Buffer const& _buffer) const;

    Texture CreateTexture(uint32_t _width, uint32_t _height, uint32_t _depth,
                          VkImageType _type, VkFormat _format, VkImageUsageFlags _usage);

    Texture CreateTexture(uint32_t _width, uint32_t _height, uint32_t _depth,
                          uint32_t _mip_count,
                          VkImageType _type, VkFormat _format, VkImageUsageFlags _usage);

    Texture CreateTexture(uint32_t _width, uint32_t _height, uint32_t _depth,
                          VkImageType _type, VkFormat _format, VkImageUsageFlags _usage,
                          std::function<void(void*)> const& _upload_callback);
    void DestroyTexture(Texture const& _texture) const;

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

    using SynchronisedRoutine = std::function<void(Context&)>;
    void ScheduleRoutine(VkFence _fence, SynchronisedRoutine _routine);
    void ScheduleFenceDestroy(VkFence _fence);
    void TryScheduledRoutines();
    void WaitScheduledRoutines();
    std::unordered_map<VkFence, std::vector<SynchronisedRoutine>> pending_routines = {};
    std::unordered_set<VkFence> pending_fences = {};
};

namespace RenderGraph
{

namespace Layout
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
    uint32_t group_instance_count;
    std::vector<StageBindingGroup> binding_groups;
    std::vector<PushConstantRange> push_constants;
    GraphicsStage graphics;
};

enum NodeDispatchMode {
    NodeDispatchMode_Graphics,
    NodeDispatchMode_Compute,
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
    NodeDispatchMode mode;
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

enum BindingType {
    BindingType_Shader,
    BindingType_Framebuffer,
    BindingType_PushConstantRange,
    BindingType_VertexInput,
    BindingType_Present,
};

struct NodeBindingPoint {
    DispatchNode const* node;
    uint32_t stage_index;
    BindingType type;
    union {
        ShaderLocation shader;
        FramebufferLocation framebuffer;
        PushConstantsLocation push_constants;
    };
};

enum EdgeType {
    EdgeType_Image,
    EdgeType_Buffer,
};

struct GraphEdge {
    EdgeType type;
    NodeBindingPoint source;
    NodeBindingPoint target;
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

    void InsertEdge(GraphEdge const& edge);
    std::vector<vktk::Context::RenderPassAttachment> RenderPassDescription(
        uint32_t _node_index,
        VkFormat const* _swapchain_format = nullptr) const;

    static BindingPointSync BindingPointSyncRequirements(EdgeType type, NodeBindingPoint const& binding_point);
};

} // namespace Layout

namespace Binding
{

struct ShaderResource {
    uint32_t descriptor_count;
    union {
        VkDescriptorImageInfo const* images;
        VkDescriptorBufferInfo const* buffers;
    };
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

struct DispatchInstance {
    GraphicsCommand graphics;
    std::vector<std::unique_ptr<uint8_t[]>> push_constants;
};

struct DispatchStage {
    std::vector<DispatchInstance> instances;
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

} // namespace Binding

namespace Runtime
{

struct StageBindingGroupStatic {
    vktk::DescriptorPool pool;
    std::vector<VkDescriptorSet> sets;
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

struct DispatchRuntime {
    vktk::Context* vulkan;
    Layout::DispatchGraph const* graph;
    std::vector<DispatchNodeStatic> nodes_static;
    std::vector<DispatchNodeFramebuffer> nodes_framebuffer;
    std::vector<Binding::DispatchNode> nodes_bindings;

    void ClearStaticRuntime();
    void ClearFramebufferRuntime();

    void GenerateStaticRuntime(Binding::PNextMap const& _pNext_map = {});
    void GenerateFramebufferRuntime(VkFormat const* _swapchain_format = nullptr);
    void Dispatch(vktk::CommandBuffer& _command_buffer) const;

    void BindSwapchain(Layout::DispatchNode const* _node,
                       vktk::Swapchain const& _swapchain);
    void BindAttachments(Layout::DispatchNode const* _node,
                         numtk::vec2u const& _size,
                         std::vector<VkImageView> const& _attachments);
    void BindFramebufferArea(Layout::DispatchNode const* _node,
                             Binding::FramebufferArea const& _area);
    void BindGraphicsCommand(Layout::DispatchNode const* _node,
                             uint32_t _stage_index,
                             uint32_t _instance_index,
                             Binding::GraphicsCommand const& _command);

    void* BindPushConstants(Layout::NodeBindingPoint const& _location,
                            uint32_t _instance_index);
    VkWriteDescriptorSet BindShaderResource(Layout::NodeBindingPoint const& _location,
                                            uint32_t _instance_index,
                                            Binding::ShaderResource const& _resources);
};

} // namespace Runtime

} // namespace RenderGraph

} // namespace vktk
