#pragma once

#include <cstdint>
#include <vector>

#include <vulkan/vulkan.h>

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

    uint32_t index;

    VkFramebuffer AcquireNext(VkSemaphore _signal);
    void Release();
};

struct Buffer {
    VkBuffer buffer;
    Allocation alloc;
    //VkDeviceMemory memory;

    operator VkBuffer() { return buffer; }
};

struct Texture {
    VkImage image;
    VkDeviceMemory memory;
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
    uint32_t family_index;
    VkQueue queue;
    operator VkQueue() { return queue; }
    VkCommandPool command_pool;
};

struct CommandBuffer {
    Queue const& queue;
    VkCommandBuffer handle;
    operator VkCommandBuffer() { return handle; }
    void Begin();

    void BufferCopy(VkBuffer _dst, uint64_t _dst_offset,
                    VkBuffer _src, uint64_t _src_offset, uint64_t _size);

#if 0
    void BufferCopy(Buffer const& _dst, uint64_t _dst_offset,
                    Buffer const& _src, uint64_t _src_offset, uint64_t _size);
#endif

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

    void End();
};

struct Context
{
    Context(uint64_t _hinstance, uint64_t _hwindow);
    ~Context();

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

    Swapchain CreateSwapchain(uint32_t const _size[2]);

    Buffer CreateBuffer(uint32_t _size,
                        VkBufferUsageFlags _usage);
    Buffer CreateBuffer(uint32_t _size,
                        VkBufferUsageFlags _usage,
                        VkMemoryPropertyFlags _memory_flags);
    void DestroyBuffer(Buffer& _buffer);

    Texture CreateTexture(uint32_t _width, uint32_t _height, uint32_t _depth,
                          VkImageType _type, VkFormat _format, VkImageUsageFlags _usage);


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
};

} // namespace vktk
