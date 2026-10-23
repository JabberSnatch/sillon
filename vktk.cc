
#if defined(_WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#define NOMINMAX
#include <windows.h>
#endif

#if defined(__unix__)
#define VK_USE_PLATFORM_XLIB_KHR
#include <X11/Xlib.h>
#endif

#include <vulkan/vulkan.h>

#include "vktk.hh"
#include "toolkits/numtk.hh"

#include <shaderc/shaderc.hpp>

#include <cassert>
#include <cstring>
#include <iostream>
#include <numeric>
#include <vector>
#include <string>

namespace
{

static char const* kInstanceExtensions[] = {
    "VK_KHR_surface",
#ifdef _WIN32
    "VK_KHR_win32_surface",
#endif

#ifdef __unix__
    "VK_KHR_xlib_surface",
#endif
};
static std::uint32_t const kInstanceExtCount =
    sizeof(kInstanceExtensions) / sizeof(kInstanceExtensions[0]);

static char const* kDeviceExtensions[] = {
    "VK_KHR_swapchain",
    "VK_EXT_scalar_block_layout"
};
static std::uint32_t const kDeviceExtCount =
    sizeof(kDeviceExtensions) / sizeof(kDeviceExtensions[0]);

} // namespace

namespace vktk
{

Context::Context(uint64_t _hinstance, uint64_t _hwindow)
{
    {
        VkApplicationInfo app_info{};
        app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app_info.pNext = nullptr;
        app_info.pApplicationName = "";
        app_info.applicationVersion = 0;
        app_info.pEngineName = "";
        app_info.engineVersion = 0;
        app_info.apiVersion = VK_API_VERSION_1_3;

        VkInstanceCreateInfo instance_info{};
        instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        instance_info.pNext = nullptr;
        instance_info.flags = 0;
        instance_info.pApplicationInfo = &app_info;
        instance_info.enabledLayerCount = 0;
        instance_info.ppEnabledLayerNames = nullptr;
        instance_info.enabledExtensionCount = kInstanceExtCount;
        instance_info.ppEnabledExtensionNames = kInstanceExtensions;

        CHECKCALL(vkCreateInstance, &instance_info, nullptr, &instance);
    }

    #define INSTANCE instance
    #define TABLE fp
    kRequiredInstanceProcs(GetInstanceProcAddress_XMacro)
    #undef INSTANCE
    #undef TABLE

    {
        uint32_t dev_count = 0u;
        vkEnumeratePhysicalDevices(instance, &dev_count, nullptr);
        std::vector<VkPhysicalDevice> availableDevices{ dev_count };
        vkEnumeratePhysicalDevices(instance, &dev_count, availableDevices.data());

        uint32_t selected_index = 0u;
        for (uint32_t devIndex = 1u; devIndex < dev_count; ++devIndex)
        {
            VkPhysicalDeviceProperties properties{};
            vkGetPhysicalDeviceProperties(availableDevices[devIndex], &properties);

            if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
            //if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
            {
                selected_index = devIndex;
                break;
            }
        }

        {
            physical_device = availableDevices[selected_index];
            vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);

            VkPhysicalDeviceProperties properties{};
            vkGetPhysicalDeviceProperties(physical_device, &properties);
            std::cout << "[vktk] [" << properties.deviceName << "]" << std::endl;
        }
    }

#ifdef _WIN32
    {
        VkWin32SurfaceCreateInfoKHR create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
        create_info.pNext = nullptr;
        create_info.flags = 0;
        create_info.hinstance = (HINSTANCE)_hinstance;
        create_info.hwnd = (HWND)_hwindow;
        CHECKCALL(vkCreateWin32SurfaceKHR, instance, &create_info, nullptr, &surface);
    }
#endif

#ifdef __unix__
    {
        VkXlibSurfaceCreateInfoKHR create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
        create_info.pNext = nullptr;
        create_info.flags = 0u;
        create_info.dpy = (Display*)_hinstance;
        create_info.window = (Window)_hwindow;
        CHECKCALL(vkCreateXlibSurfaceKHR, instance, &create_info, nullptr, &surface);
    }
#endif

    {
        std::uint32_t queueFamilyCount = 0u;
        vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queueFamilyCount,
                                                 nullptr);
        std::vector<VkQueueFamilyProperties> queue_properties{ queueFamilyCount };
        vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queueFamilyCount,
                                                 queue_properties.data());

        std::uint32_t present_queue_index = ~0u;
        std::uint32_t copy_queue_index = ~0u;
        for (std::uint32_t index = 0u; index < queue_properties.size(); ++index)
        {
            VkBool32 canPresent = VK_FALSE;
            fp.GetPhysicalDeviceSurfaceSupportKHR(physical_device, index, surface,
                                                  &canPresent);
            if (present_queue_index == ~0u
                && queue_properties[index].queueFlags & VK_QUEUE_GRAPHICS_BIT
                && canPresent == VK_TRUE)
                present_queue_index = index;

            if (queue_properties[index].queueFlags &
                (VK_QUEUE_COMPUTE_BIT | VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_TRANSFER_BIT))
            {
                if (copy_queue_index == ~0u)
                    copy_queue_index = index;

                else if (
                    (queue_properties[index].queueFlags
                        & VK_QUEUE_TRANSFER_BIT)
                    && !(queue_properties[index].queueFlags
                         & (VK_QUEUE_COMPUTE_BIT | VK_QUEUE_GRAPHICS_BIT))
                    && (queue_properties[copy_queue_index].queueFlags
                        & (VK_QUEUE_COMPUTE_BIT | VK_QUEUE_GRAPHICS_BIT)))
                    copy_queue_index = index;
            }
        }

        static float const kQueuePriority = 0.f;
        VkDeviceQueueCreateInfo queue_info[2];
        {
            VkDeviceQueueCreateInfo& present_queue_info = queue_info[0];
            present_queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            present_queue_info.pNext = nullptr;
            present_queue_info.flags = 0;
            present_queue_info.queueFamilyIndex = present_queue_index;
            present_queue_info.queueCount = 1;
            present_queue_info.pQueuePriorities = &kQueuePriority;
        }

        {
            VkDeviceQueueCreateInfo& copy_queue_info = queue_info[1];
            copy_queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            copy_queue_info.pNext = nullptr;
            copy_queue_info.flags = 0;
            copy_queue_info.queueFamilyIndex = copy_queue_index;
            copy_queue_info.queueCount = 1;
            copy_queue_info.pQueuePriorities = &kQueuePriority;
        }

        VkPhysicalDeviceVulkan12Features Vulkan12Features = {};
        Vulkan12Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        Vulkan12Features.runtimeDescriptorArray = VK_TRUE;
        Vulkan12Features.descriptorBindingVariableDescriptorCount = VK_TRUE;

        VkPhysicalDeviceMaintenance5Features Maintenance5Features = {};
        Maintenance5Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_5_FEATURES;
        Maintenance5Features.maintenance5 = VK_TRUE;

        VkPhysicalDeviceFeatures2 Features2 = {};
        Features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;

        Features2.pNext = &Vulkan12Features;
        Vulkan12Features.pNext = &Maintenance5Features;
        vkGetPhysicalDeviceFeatures2(physical_device, &Features2);

        VkDeviceCreateInfo device_info{};
        device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        device_info.pNext = &Features2;
        device_info.flags = 0;
        device_info.queueCreateInfoCount = 2;
        device_info.pQueueCreateInfos = queue_info;
        device_info.enabledLayerCount = 0;
        device_info.ppEnabledLayerNames = nullptr;
        device_info.enabledExtensionCount = kDeviceExtCount;
        device_info.ppEnabledExtensionNames = kDeviceExtensions;
        device_info.pEnabledFeatures = nullptr;

        CHECKCALL(vkCreateDevice, physical_device, &device_info, nullptr, &device);

        {
            VkCommandPoolCreateInfo command_pool_info{};
            command_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            command_pool_info.pNext = nullptr;
            command_pool_info.flags = 0u;
            command_pool_info.queueFamilyIndex = present_queue_index;
            CHECKCALL(vkCreateCommandPool,
                      device, &command_pool_info, nullptr, &present_queue.command_pool);

            vkGetDeviceQueue(device, present_queue_index, 0, &present_queue.queue);
            present_queue.context = this;
            present_queue.family_index = present_queue_index;
        }

        {
            VkCommandPoolCreateInfo command_pool_info{};
            command_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            command_pool_info.pNext = nullptr;
            command_pool_info.flags = 0u;
            command_pool_info.queueFamilyIndex = copy_queue_index;
            CHECKCALL(vkCreateCommandPool,
                      device, &command_pool_info, nullptr, &copy_queue.command_pool);

            vkGetDeviceQueue(device, copy_queue_index, 0, &copy_queue.queue);
            copy_queue.context = this;
            copy_queue.family_index = copy_queue_index;
        }
    }

    allocator.device = device;
    allocator.memory_properties = memory_properties;

    #define DEVICE device
    #define TABLE fp
    kRequiredDeviceProcs(GetDeviceProcAddress_XMacro)
    #undef DEVICE
    #undef TABLE
}

Context::~Context()
{
    WaitScheduledRoutines();

#if defined(_WIN32) || defined(__unix__)
    vkDestroySurfaceKHR(instance, surface, nullptr);
#endif

    vkDestroyCommandPool(device, present_queue.command_pool, nullptr);
    vkDestroyCommandPool(device, copy_queue.command_pool, nullptr);

    allocator.Release();

    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);
}


VkShaderModule Context::CompileShader_GLSL(VkShaderStageFlagBits _shaderStage,
                                           char const* _shaderName,
                                           std::vector<std::string> const& _code)
{
    return CompileShader_SPIRV(
        CompileShader_GLSLtoSPIRV(
            _shaderStage, _shaderName, _code));
}

VkShaderModule Context::CompileShader_GLSL(VkShaderStageFlagBits _shaderStage,
                                           char const* _shaderName,
                                           char const* _code,
                                           size_t _codeSize)
{
    return CompileShader_SPIRV(
        CompileShader_GLSLtoSPIRV(
            _shaderStage, _shaderName, _code, _codeSize));
}


std::vector<uint32_t> Context::CompileShader_GLSLtoSPIRV(VkShaderStageFlagBits _shaderStage,
                                                         char const* _shaderName,
                                                         std::vector<std::string> const& _code)
{
    std::string full_source =
        std::accumulate(_code.begin(), _code.end(), std::string(""),
                        [](std::string const& a, std::string const& b){ return a + "\n" + b; });
    return CompileShader_GLSLtoSPIRV(_shaderStage,
                                     _shaderName,
                                     full_source.c_str(),
                                     full_source.size());
}

std::vector<uint32_t> Context::CompileShader_GLSLtoSPIRV(VkShaderStageFlagBits _shaderStage,
                                                         char const* _shaderName,
                                                         char const* _code,
                                                         size_t _codeSize)
{
    shaderc_shader_kind shader_kind = shaderc_glsl_infer_from_source;
    switch (_shaderStage)
    {
    case VK_SHADER_STAGE_VERTEX_BIT:
        shader_kind = shaderc_vertex_shader; break;
    case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT:
        shader_kind = shaderc_tess_control_shader; break;
    case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT:
        shader_kind = shaderc_tess_evaluation_shader; break;
    case VK_SHADER_STAGE_GEOMETRY_BIT:
        shader_kind = shaderc_geometry_shader; break;
    case VK_SHADER_STAGE_FRAGMENT_BIT:
        shader_kind = shaderc_fragment_shader; break;
    case VK_SHADER_STAGE_COMPUTE_BIT:
        shader_kind = shaderc_compute_shader; break;
    default: break;
    }

    shaderc::CompileOptions compile_options = {};
#ifndef _NDEBUG
    compile_options.SetGenerateDebugInfo();
    compile_options.SetOptimizationLevel(shaderc_optimization_level_zero);
#endif

    shaderc::SpvCompilationResult compilation =
        shaderc::Compiler{}.CompileGlslToSpv(_code, _codeSize, shader_kind, _shaderName,
                                             compile_options);

    if (compilation.GetCompilationStatus() != shaderc_compilation_status_success)
    {
        std::cout << compilation.GetErrorMessage() << std::endl;
        return {};
    }

    return { compilation.begin(), compilation.end() };
}

VkShaderModule Context::CompileShader_SPIRV(std::vector<uint32_t> const& _code)
{
    return CompileShader_SPIRV(_code.data(), _code.size() * 4);
}

VkShaderModule Context::CompileShader_SPIRV(uint32_t const* _code,
                                            size_t _codeSize)
{
    VkShaderModuleCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    create_info.pNext = nullptr;
    create_info.flags = 0u;
    create_info.codeSize = _codeSize;
    create_info.pCode = _code;

    VkShaderModule output{};
    CHECKCALL(vkCreateShaderModule, device, &create_info, nullptr, &output);
    return output;
}


void Queue::Submit(VkCommandBuffer const* _command_buffers,
                   uint32_t _command_buffer_count,
                   VkSemaphore* _signal_semaphore,
                   VkSemaphore const* _wait_semaphores,
                   VkPipelineStageFlags const* _wait_stages,
                   uint32_t _wait_count,
                   VkFence* _signal_fence) const
{
    if (_signal_semaphore)
    {
        VkSemaphoreCreateInfo semaphore_info = {};
        semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        semaphore_info.pNext = nullptr;
        semaphore_info.flags = 0;
        CHECKCALL(vkCreateSemaphore, context->device, &semaphore_info, nullptr, _signal_semaphore);
    }

    if (_signal_fence && *_signal_fence == VK_NULL_HANDLE)
    {
        *_signal_fence = context->fence_pool.AllocateFence(*context);
        ++context->fence_count;
    }

    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.pNext = nullptr;
    submit_info.waitSemaphoreCount = _wait_count;
    submit_info.pWaitSemaphores = _wait_semaphores;
    submit_info.pWaitDstStageMask = _wait_stages;
    submit_info.commandBufferCount = _command_buffer_count;
    submit_info.pCommandBuffers = _command_buffers;
    submit_info.signalSemaphoreCount = _signal_semaphore ? 1u : 0u;
    submit_info.pSignalSemaphores = _signal_semaphore;
    vkQueueSubmit(queue, 1, &submit_info, _signal_fence ? *_signal_fence : VK_NULL_HANDLE);
}

VkFence FencePool::AllocateFence(Context& context_)
{
    Handle result = free_list.Reserve();

    if (free_list.RequiredSize() > fences.size())
    {
        fences.resize(free_list.RequiredSize());
        VkFenceCreateInfo fence_info = {};
        fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fence_info.pNext = nullptr;
        fence_info.flags = 0;
        CHECKCALL(vkCreateFence, context_.device, &fence_info, nullptr, &fences[result]);
    }

    return fences[result];
}

void FencePool::ResetFence(Context const& context_, VkFence fence_)
{
    auto fence_it = std::find(fences.begin(), fences.end(), fence_);
    if (fence_it == fences.end())
        return;
    uint64_t index = std::distance(fences.begin(), fence_it);

    CHECKCALL(vkResetFences, context_.device, 1, &fence_);
    free_list.Release(index);
}

void CommandBuffer::Submit(VkSemaphore* _signal_semaphore,
                           VkSemaphore const* _wait_semaphores,
                           VkPipelineStageFlags const* _wait_stages,
                           uint32_t _wait_count,
                           VkFence* _signal_fence)
{
    VkFence fallback_fence = VK_NULL_HANDLE;
    if (!_signal_fence)
        _signal_fence = &fallback_fence;

    queue.Submit(&handle, 1, _signal_semaphore,
                 _wait_semaphores, _wait_stages, _wait_count,
                 _signal_fence);

#if 0
    for (Buffer const& buffer : pending_destroys)
        std::cout << "[CommandSubmit] scheduling staging buffer destruction " << std::hex
                  << buffer.buffer << std::dec << std::endl;
#endif

    queue.context->ScheduleRoutine(
        *_signal_fence,
        [handle = this->handle,
         command_pool = this->queue.command_pool,
         pending_destroys = std::move(pending_destroys)](Context& context) {

#ifndef _NDEBUG
            using RangeVector = std::vector<std::tuple<uint64_t, uint64_t, uint64_t>>;
            std::unordered_map<uint64_t, RangeVector> ranges_map = {};

            std::unordered_set<uint64_t> handles = {};
            std::unordered_map<uint64_t, std::unordered_set<uint64_t>> allocs = {};
            for (Buffer const& buffer : pending_destroys)
            {
                assert(!handles.count((uint64_t)buffer.buffer));
                handles.emplace((uint64_t)buffer.buffer);

                assert(!allocs[(uint64_t)buffer.alloc.memory].count(buffer.alloc.offset));
                allocs[(uint64_t)buffer.alloc.memory].emplace(buffer.alloc.offset);

                RangeVector& ranges = ranges_map[(uint64_t)buffer.alloc.memory];

                auto range_iterator = std::lower_bound(
                    ranges.begin(), ranges.end(),
                    buffer.alloc.offset,
                    [](auto const& _lhs, auto _rhs) -> bool {
                        return std::get<0>(_lhs) < _rhs;
                    });

                auto range_begin = (uint64_t)buffer.alloc.offset;
                auto range_end = (uint64_t)(buffer.alloc.size + buffer.alloc.offset);

                if (range_iterator != ranges.end())
                {
                    auto comp_begin = std::get<0>(*range_iterator);
                    auto comp_end = std::get<1>(*range_iterator);
                    assert(!(range_begin >= comp_begin && range_begin < comp_end));
                    assert(!(range_end > comp_begin && range_end <= comp_end));
                    assert(!(comp_begin >= range_begin && comp_begin < range_end));
                    assert(!(comp_end > range_begin && comp_end <= range_end));
                }

                if (range_iterator != ranges.begin())
                {
                    auto comp_begin = std::get<0>(*std::prev(range_iterator));
                    auto comp_end = std::get<1>(*std::prev(range_iterator));
                    assert(!(range_begin >= comp_begin && range_begin < comp_end));
                    assert(!(range_end > comp_begin && range_end <= comp_end));
                    assert(!(comp_begin >= range_begin && comp_begin < range_end));
                    assert(!(comp_end > range_begin && comp_end <= range_end));
                }

                ranges.insert(range_iterator, {
                    (uint64_t)buffer.alloc.offset,
                    (uint64_t)(buffer.alloc.size + buffer.alloc.offset),
                    (uint64_t)buffer.buffer
                });
            }
#endif

            for (Buffer const& buffer : pending_destroys)
            {
#if 0
                std::cout << "[ScheduledRoutine] destroying buffer " << std::hex
                          << buffer.buffer << std::dec << std::endl;
#endif
                context.DestroyBuffer(buffer);
            }

            vkFreeCommandBuffers(context.device, command_pool, 1, &handle);
        });

    if (_signal_fence == &fallback_fence)
        queue.context->ScheduleFenceDestroy(fallback_fence);

    pending_destroys = {};
}

void CommandBuffer::Begin()
{
    VkCommandBufferBeginInfo cmd_begin_info{};
    cmd_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cmd_begin_info.pNext = nullptr;
    cmd_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    cmd_begin_info.pInheritanceInfo = nullptr;
    vkBeginCommandBuffer(handle, &cmd_begin_info);
}

void CommandBuffer::BufferCopy(VkBuffer _dst, uint64_t _dst_offset,
                               VkBuffer _src, uint64_t _src_offset, uint64_t _size)
{
    VkBufferCopy buffer_copy = {};
    buffer_copy.srcOffset = _src_offset;
    buffer_copy.dstOffset = _dst_offset;
    buffer_copy.size = _size;
    vkCmdCopyBuffer(handle, _src, _dst, 1, &buffer_copy);
}

#if 0
void CommandBuffer::BufferCopy(Buffer const& _dst, uint64_t _dst_offset,
                               Buffer const& _src, uint64_t _src_offset, uint64_t _size)
{
    VkBufferCopy buffer_copy = {};
    buffer_copy.srcOffset = _src.alloc.offset + _src_offset;
    buffer_copy.dstOffset = _dst.alloc.offset + _dst_offset;
    buffer_copy.size = _size;
    vkCmdCopyBuffer(handle, _src.buffer, _dst.buffer, 1, &buffer_copy);
}
#endif

void CommandBuffer::UpdateTexture(Texture _texture, VkOffset3D _offset, VkExtent3D _extent,
                                  std::function<void(void*)> const& _upload_callback)
{
    UpdateTexture(_texture, _offset, _extent, 0, _upload_callback);
}

void CommandBuffer::UpdateTexture(Texture _texture, VkOffset3D _offset, VkExtent3D _extent,
                                  uint32_t _mip_level,
                                  std::function<void(void*)> const& _upload_callback)
{
    uint64_t staging_size = VkFormatBytesPerUnit(_texture.format)
        * _extent.width
        * _extent.height
        * _extent.depth;
    Buffer staging = queue.context->CreateBuffer(staging_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);

    void* mapped_data = staging.alloc.map;
    _upload_callback(mapped_data);

    BufferTransitions(VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                      {
                          { staging.buffer, VK_ACCESS_HOST_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT }
                      });

    VkBufferImageCopy copy_region{};
    copy_region.bufferOffset = 0u;
    copy_region.bufferRowLength = 0u;
    copy_region.bufferImageHeight = 0u;
    copy_region.imageSubresource = VkImageSubresourceLayers {
        VK_IMAGE_ASPECT_COLOR_BIT,
        _mip_level, 0u, 1u
    };
    copy_region.imageOffset = _offset;
    copy_region.imageExtent = _extent;

    vkCmdCopyBufferToImage(*this,
                           staging.buffer,
                           _texture.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1,
                           &copy_region);

#if 0
    std::cout << "[CommandUpdateTexture] scheduled staging buffer destruction "
              << std::hex << staging.buffer << std::dec << std::endl;
#endif
    pending_destroys.emplace_back(staging);
}

void CommandBuffer::BufferTransitions(VkPipelineStageFlags _src_stage,
                                      VkPipelineStageFlags _dst_stage,
                                      std::vector<BufferTransition> const& _transitions)
{
    std::vector<VkBufferMemoryBarrier> buffer_barriers(_transitions.size());
    for (std::size_t index = 0; index < _transitions.size(); ++index)
    {
        BufferTransition const& transition = _transitions[index];
        VkBufferMemoryBarrier& barrier = buffer_barriers[index];

        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        barrier.pNext = nullptr;
        barrier.srcAccessMask = transition.src_access;
        barrier.dstAccessMask = transition.dst_access;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = transition.buffer;
        barrier.offset = 0;
        barrier.size = VK_WHOLE_SIZE;
    }

    vkCmdPipelineBarrier(handle, _src_stage, _dst_stage, 0, 0, nullptr,
                         buffer_barriers.size(), buffer_barriers.data(),
                         0, nullptr);
}

void CommandBuffer::BufferTransitions(CommandBuffer const& _dst,
                                      VkPipelineStageFlags _src_stage,
                                      VkPipelineStageFlags _dst_stage,
                                      std::vector<BufferTransition> const& _transitions)
{
    std::vector<VkBufferMemoryBarrier> src_barriers(_transitions.size());
    std::vector<VkBufferMemoryBarrier> dst_barriers(_transitions.size());
    for (std::size_t index = 0; index < _transitions.size(); ++index)
    {
        BufferTransition const& transition = _transitions[index];
        VkBufferMemoryBarrier& src_barrier = src_barriers[index];
        VkBufferMemoryBarrier& dst_barrier = dst_barriers[index];

        src_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        src_barrier.pNext = nullptr;
        src_barrier.srcAccessMask = transition.src_access;
        src_barrier.dstAccessMask = VK_ACCESS_NONE;
        src_barrier.srcQueueFamilyIndex = queue.family_index;
        src_barrier.dstQueueFamilyIndex = _dst.queue.family_index;
        src_barrier.buffer = transition.buffer;
        src_barrier.offset = 0;
        src_barrier.size = VK_WHOLE_SIZE;

        dst_barrier = src_barrier;
        dst_barrier.srcAccessMask = VK_ACCESS_NONE;
        dst_barrier.dstAccessMask = transition.dst_access;
    }

    vkCmdPipelineBarrier(handle, _src_stage, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                         0, 0, nullptr,
                         src_barriers.size(), src_barriers.data(),
                         0, nullptr);
    vkCmdPipelineBarrier(_dst.handle, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, _dst_stage,
                         0, 0, nullptr,
                         dst_barriers.size(), dst_barriers.data(),
                         0, nullptr);
}

void CommandBuffer::ImageTransitions(VkPipelineStageFlags _src_stage,
                                     VkPipelineStageFlags _dst_stage,
                                     std::vector<ImageTransition> const& _transitions)
{
    std::vector<VkImageMemoryBarrier> barriers(_transitions.size());
    for (std::size_t index = 0; index < _transitions.size(); ++index)
    {
        ImageTransition const& transition = _transitions[index];
        VkImageMemoryBarrier& barrier = barriers[index];

        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.pNext = nullptr;
        barrier.srcAccessMask = transition.src_access;
        barrier.dstAccessMask = transition.dst_access;
        barrier.oldLayout = transition.old_layout;
        barrier.newLayout = transition.new_layout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = transition.image;
        barrier.subresourceRange = {
            transition.aspect_flags,
            0, VK_REMAINING_MIP_LEVELS,
            0, VK_REMAINING_ARRAY_LAYERS
        };
    }

    vkCmdPipelineBarrier(handle, _src_stage, _dst_stage,
                         0, 0, nullptr, 0, nullptr,
                         barriers.size(), barriers.data());
}

void CommandBuffer::BeginRenderPass(VkExtent2D _size,
                                    VkFramebuffer _target,
                                    VkRenderPass _render_pass,
                                    std::vector<VkClearValue> const& _clear_values)
{
    BeginRenderPass(numtk::bounds2i{ { 0, 0 }, { (int32_t)_size.width, (int32_t)_size.height } },
                    _target, _render_pass, _clear_values);
}

void CommandBuffer::BeginRenderPass(numtk::bounds2i const& _viewport,
                                    VkFramebuffer _target,
                                    VkRenderPass _render_pass,
                                    std::vector<VkClearValue> const& _clear_values)
{
    VkViewport viewport{};
    viewport.x = (float)_viewport.min.x(); viewport.y = (float)_viewport.min.y();
    viewport.width = (float)_viewport.extent.x();
    viewport.height = (float)_viewport.extent.y();
    viewport.minDepth = 0.f; viewport.maxDepth = 1.f;
    vkCmdSetViewport(handle, 0, 1, &viewport);

    VkRect2D scissor{
        VkOffset2D{ _viewport.min.x(), _viewport.min.y() },
        VkExtent2D{ (uint32_t)_viewport.extent.x(), (uint32_t)_viewport.extent.y() }
    };
    vkCmdSetScissor(handle, 0, 1, &scissor);

    VkRenderPassBeginInfo rp_begin_info{};
    rp_begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp_begin_info.pNext = nullptr;
    rp_begin_info.renderPass = _render_pass;
    rp_begin_info.framebuffer = _target;
    rp_begin_info.renderArea = scissor;
    rp_begin_info.clearValueCount = _clear_values.size();
    rp_begin_info.pClearValues = _clear_values.data();
    vkCmdBeginRenderPass(handle, &rp_begin_info, VK_SUBPASS_CONTENTS_INLINE);
}

void CommandBuffer::End()
{
    vkEndCommandBuffer(handle);
}

Allocation Allocator::Allocate(VkMemoryPropertyFlags _memory_flags,
                            uint32_t _size)
{
    uint32_t memory_type = SelectMemoryType(_memory_flags, _size);

    auto value_iterator = std::lower_bound(
        heaps.begin(), heaps.end(), memory_type,
        [](ChunkStorage const& _lhs, uint32_t _rhs) {
            return _lhs.memory_type < _rhs;
        });
    if (value_iterator == heaps.end()
        || value_iterator->memory_type != memory_type)
        value_iterator = heaps.insert(value_iterator, ChunkStorage{ memory_type, {}, {} });

    ChunkStorage& storage = *value_iterator;
    if (storage.chunks.empty())
        AllocateChunk(storage);

    std::size_t range_index = ~0ull;
    for (std::size_t index = 0; index < storage.free_list.size(); ++index)
    {
        BlockRange& range = storage.free_list[index];
        if ((range.second - range.first)*kBlockSize > _size)
        {
            range_index = index;
            break;
        }
    }

    if (range_index == ~0ull)
    {
        range_index = storage.free_list.size();
        AllocateChunk(storage);
    }

    BlockRange& range = storage.free_list[range_index];
    std::size_t chunk_index = range.first / kChunkBlockCount;
    VkDeviceMemory chunk = storage.chunks[chunk_index];

    VkDeviceSize offset = (range.first % kChunkBlockCount) * kBlockSize;
    uint64_t block_count = (_size / kBlockSize) + ((_size % kBlockSize != 0) ? 1 : 0);
    range.first = range.first + block_count;
    if (range.first == range.second)
        storage.free_list.erase(storage.free_list.begin() + range_index);

    void* map = nullptr;
    if (!storage.maps.empty())
        map = ((uint8_t*)storage.maps[chunk_index]) + offset;

    return Allocation{ memory_type, chunk, offset, _size, map };
}

Allocation Allocator::Allocate(VkMemoryPropertyFlags _memory_flags,
                            uint32_t _size,
                            VkDeviceSize _alignment)
{
    VkDeviceSize padding = 0;
    if (_alignment > kBlockSize)
        padding = _alignment-1;
    else
        return Allocate(_memory_flags, _size);

    Allocation alloc = Allocate(_memory_flags, (_size + padding));
    alloc.offset += _alignment - (alloc.offset & padding);
    return alloc;
}

void Allocator::Free(Allocation const& _alloc)
{
    auto heap_iterator = std::lower_bound(heaps.begin(), heaps.end(), _alloc.memory_type,
                                          [](ChunkStorage const& _lhs, uint32_t _rhs) {
                                              return _lhs.memory_type < _rhs;
                                          });
    assert(heap_iterator != heaps.end());

    ChunkStorage& storage = *heap_iterator;
    uint32_t chunk_index = std::distance(
        storage.chunks.begin(),
        std::find_if(storage.chunks.begin(), storage.chunks.end(),
                     [&_alloc](VkDeviceMemory _memory){
                         return _memory == _alloc.memory;
                     })
    );
    uint64_t alloc_begin = (_alloc.offset / kBlockSize) + chunk_index * kChunkBlockCount;
    uint64_t alloc_end = alloc_begin + (_alloc.size / kBlockSize) + ((_alloc.size % kBlockSize != 0) ? 1 : 0);

    auto range_iterator = std::lower_bound(storage.free_list.begin(), storage.free_list.end(), alloc_begin,
                                           [](BlockRange const& _lhs, uint64_t _rhs) {
                                               return _lhs.first < _rhs;
                                           });
    assert(range_iterator != storage.free_list.end());

    if (range_iterator != storage.free_list.begin()
        && std::prev(range_iterator)->second == alloc_begin)
    {
        range_iterator = std::prev(range_iterator);
        range_iterator->second = alloc_end;
    }
    else
        range_iterator = storage.free_list.insert(range_iterator, { alloc_begin, alloc_end });

    if (range_iterator->second == std::next(range_iterator)->first)
    {
        range_iterator->second = std::next(range_iterator)->second;
        storage.free_list.erase(std::next(range_iterator));
    }

#ifndef _NDEBUG
    for (size_t index = 0; index < storage.free_list.size(); ++index)
    {
        auto range = storage.free_list[index];
        for (size_t comp_index = index+1; comp_index < storage.free_list.size(); ++comp_index)
        {
            auto comp_range = storage.free_list[comp_index];
            assert(!(range.first >= comp_range.first && range.first < comp_range.second));
            assert(!(range.second >= comp_range.first && range.second < comp_range.second));
            assert(!(comp_range.first >= range.first && comp_range.first < range.second));
            assert(!(comp_range.second >= range.first && comp_range.second < range.second));
        }
    }
#endif
}

uint32_t Allocator::SelectMemoryType(VkMemoryPropertyFlags _flags, uint32_t _required_size)
{
    for (uint32_t index = 0u; index < memory_properties.memoryTypeCount; ++index)
    {
        VkMemoryType const& memory_type = memory_properties.memoryTypes[index];
        VkMemoryHeap const& memory_heap = memory_properties.memoryHeaps[memory_type.heapIndex];
        if ((memory_type.propertyFlags & _flags) == _flags
            && memory_heap.size >= _required_size)
        {
            return index;
        }
    }
    return ~0u;
}

void Allocator::AllocateChunk(ChunkStorage& _storage)
{
    VkMemoryAllocateInfo allocate_info{};
    allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate_info.pNext = nullptr;
    allocate_info.allocationSize = kChunkSize;
    allocate_info.memoryTypeIndex = _storage.memory_type;
    VkDeviceMemory device_memory = {};
    CHECKCALL(vkAllocateMemory, device, &allocate_info, nullptr, &device_memory);

    allocated_memory += kChunkSize;

    _storage.free_list.push_back({
        _storage.chunks.size() * kChunkBlockCount,
        (_storage.chunks.size()+1) * kChunkBlockCount
    });
    _storage.chunks.push_back(device_memory);

    if (memory_properties.memoryTypes[_storage.memory_type].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
    {
        void* map = nullptr;
        CHECKCALL(vkMapMemory, device, device_memory, 0, VK_WHOLE_SIZE, 0u, &map);
        _storage.maps.push_back(map);
    }
}

void Allocator::Release()
{
    for (ChunkStorage& heap : heaps)
        for (VkDeviceMemory chunk : heap.chunks)
        {
            if (!heap.maps.empty())
                vkUnmapMemory(device, chunk);
            vkFreeMemory(device, chunk, nullptr);
        }

    heaps.clear();
}

CommandBuffer Context::CreateCommandBuffer(Queue const& queue)
{
    VkCommandBufferAllocateInfo command_buffer_info{};
    command_buffer_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    command_buffer_info.pNext = nullptr;
    command_buffer_info.commandPool = queue.command_pool;
    command_buffer_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command_buffer_info.commandBufferCount = 1;

    CommandBuffer command_buffer{ queue, VK_NULL_HANDLE };
    CHECKCALL(vkAllocateCommandBuffers, device, &command_buffer_info, &command_buffer.handle);

    return command_buffer;
}

VkRenderPass Context::CreateRenderPass(std::vector<RenderPassAttachment> const& _attachments)
{
    auto it = std::find_if(_attachments.begin(), _attachments.end(),
                           [](RenderPassAttachment const& attachment) {
                               return (
                                   attachment.format >= VK_FORMAT_D16_UNORM
                                   && attachment.format < VK_FORMAT_BC1_RGB_UNORM_BLOCK);
                           });

    bool has_depth = (it != _attachments.end());
    if (has_depth)
    {
        std::vector<RenderPassAttachment> copy = _attachments;
        uint32_t index = std::distance(_attachments.begin(), it);
        copy.erase(copy.begin()+index);
        return CreateRenderPass(copy, &*it);
    }
    else
        return CreateRenderPass(_attachments, nullptr);
}

VkRenderPass Context::CreateRenderPass(std::vector<RenderPassAttachment> const& _attachments,
                                       RenderPassAttachment const& _depth_attachment)
{
    return CreateRenderPass(_attachments, &_depth_attachment);
}

VkRenderPass Context::CreateRenderPass(std::vector<RenderPassAttachment> const& _attachments,
                                       RenderPassAttachment const* _depth_attachment)
{
    std::vector<VkAttachmentDescription> descriptions{};
    descriptions.reserve(_attachments.size());

    std::vector<VkAttachmentReference> references{};
    references.reserve(_attachments.size());

    for (uint32_t index = 0u; index < (uint32_t)_attachments.size(); ++index)
    {
        RenderPassAttachment const& attachment = _attachments[index];

        VkAttachmentDescription description{};
        description.flags = 0u;
        description.format = attachment.format;
        description.samples = VK_SAMPLE_COUNT_1_BIT;
        description.loadOp = attachment.loadOp;
        description.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        description.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        description.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        description.initialLayout = attachment.initialLayout;
        description.finalLayout = attachment.finalLayout;
        descriptions.push_back(description);

        VkAttachmentReference reference{};
        reference.attachment = index;
        reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        references.push_back(reference);
    }

    VkAttachmentReference depth_reference{};
    if (_depth_attachment)
    {
        VkAttachmentDescription description{};
        description.flags = 0u;
        description.format = _depth_attachment->format;
        description.samples = VK_SAMPLE_COUNT_1_BIT;
        description.loadOp = _depth_attachment->loadOp;
        description.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        description.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        description.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        description.initialLayout = _depth_attachment->initialLayout;
        description.finalLayout = _depth_attachment->finalLayout;
        descriptions.push_back(description);

        depth_reference.attachment = (uint32_t)(descriptions.size()-1);
        depth_reference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    }

    VkSubpassDescription renderSubpass{};
    renderSubpass.flags = 0u;
    renderSubpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    renderSubpass.inputAttachmentCount = 0u;
    renderSubpass.pInputAttachments = nullptr;
    renderSubpass.colorAttachmentCount = (uint32_t)references.size();
    renderSubpass.pColorAttachments = references.data();
    renderSubpass.pResolveAttachments = nullptr;
    renderSubpass.pDepthStencilAttachment = _depth_attachment ? &depth_reference : nullptr;
    renderSubpass.preserveAttachmentCount = 0u;
    renderSubpass.pPreserveAttachments = nullptr;

    VkSubpassDependency dependencies[2];
    {
        VkSubpassDependency& dependency = dependencies[0];
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
            | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstStageMask =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
            | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.srcAccessMask =
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
            | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT
            | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
            | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        dependency.dstAccessMask =
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
            | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT
            | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
            | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        dependency.dependencyFlags = 0;
    }

    {
        VkSubpassDependency& dependency = dependencies[1];
        dependency.srcSubpass = 0;
        dependency.dstSubpass = VK_SUBPASS_EXTERNAL;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
            | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
            | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
        dependency.dependencyFlags = 0;
    }

    VkRenderPassCreateInfo renderPass{};
    renderPass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPass.pNext = nullptr;
    renderPass.flags = 0u;
    renderPass.attachmentCount = (uint32_t)descriptions.size();
    renderPass.pAttachments = descriptions.data();
    renderPass.subpassCount = 1u;
    renderPass.pSubpasses = &renderSubpass;
    renderPass.dependencyCount = 1u;
    renderPass.pDependencies = dependencies;

    VkRenderPass render_pass{};
    CHECKCALL(vkCreateRenderPass, device, &renderPass, nullptr, &render_pass);

    return render_pass;
}


VkFramebuffer Context::CreateFramebuffer(VkRenderPass _render_pass,
                                         numtk::vec2u _size,
                                         std::vector<VkImageView> const& _attachments)
{
    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.pNext = nullptr;
    framebufferInfo.flags = 0u;
    framebufferInfo.renderPass = _render_pass;
    framebufferInfo.attachmentCount = _attachments.size();
    framebufferInfo.pAttachments = _attachments.data();
    framebufferInfo.width = _size[0];
    framebufferInfo.height = _size[1];
    framebufferInfo.layers = 1;

    VkFramebuffer framebuffer{};
    CHECKCALL(vkCreateFramebuffer, device, &framebufferInfo, nullptr,
              &framebuffer);
    return framebuffer;
}

VkPipeline Context::CreatePipeline(VkPipelineLayout _layout,
                                   VkRenderPass _renderPass,
                                   std::vector<PipelineStage> const& _stages)
{
    return CreatePipeline(_layout,
                          _renderPass,
                          _stages,
                          VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
                          {});
}

VkPipeline Context::CreatePipeline(VkPipelineLayout _layout,
                                   VkRenderPass _renderPass,
                                   std::vector<PipelineStage> const& _stages,
                                   VkPrimitiveTopology _topology,
                                   std::vector<VertexInput> const& _input)
{
    return CreatePipeline(_layout, _renderPass, _stages, {}, _topology, _input);
}


VkPipeline Context::CreatePipeline(VkPipelineLayout _layout,
                                   VkRenderPass _renderPass,
                                   std::vector<PipelineStage> const& _stages,
                                   std::vector<StateOverride> const& _states,
                                   VkPrimitiveTopology _topology,
                                   std::vector<VertexInput> const& _input)
{
    return CreatePipeline(_layout, _renderPass, _stages, _states, {}, _topology, _input);
}

#define VKTK_DYNAMIC_STATES(x)                                          \
    x(StateOverrideTag::CullMode, cull_mode, VK_CULL_MODE_BACK_BIT) \
    x(StateOverrideTag::DepthTestEnable, depth_test_enable, VK_TRUE)    \
    x(StateOverrideTag::DepthWriteEnable, depth_write_enable, VK_TRUE)

template <StateOverrideTag tag>
struct StateOverrideTagTraits {
    using ValueType = nullptr_t;
    static constexpr ValueType DefaultValue = nullptr;
    static inline ValueType Context::StateOverride::DataUnion::* MemberPointer = nullptr;
};

#define GENERATE_TRAITS(tag, member, default_value) \
    template <> struct StateOverrideTagTraits<tag> {\
        using ValueType = decltype(std::declval<Context::StateOverride>().state.member); \
        static constexpr ValueType DefaultValue = default_value;\
        static inline ValueType Context::StateOverride::DataUnion::* const MemberPointer \
        = &Context::StateOverride::DataUnion::member;                   \
    };                                                                  \

VKTK_DYNAMIC_STATES(GENERATE_TRAITS)

#undef GENERATE_TRAITS
#undef VKTK_DYNAMIC_STATES

template <StateOverrideTag tag>
static StateOverrideTagTraits<tag>::ValueType
DynamicStateLookup(std::unordered_map<StateOverrideTag, Context::StateOverride> const& states)
{
    if constexpr (std::is_same_v<typename StateOverrideTagTraits<tag>::ValueType, nullptr_t>)
        return nullptr;
    else
        return (!states.count(tag)
                ? StateOverrideTagTraits<tag>::DefaultValue
                : states.at(tag).state.*StateOverrideTagTraits<tag>::MemberPointer);
}

VkPipeline Context::CreatePipeline(VkPipelineLayout _layout,
                                   VkRenderPass _renderPass,
                                   std::vector<PipelineStage> const& _stages,
                                   std::vector<StateOverride> const& _states,
                                   std::vector<BlendState> const& _blend,
                                   VkPrimitiveTopology _topology,
                                   std::vector<VertexInput> const& _input)
{

    std::unordered_map<StateOverrideTag, StateOverride> states = {};
    for (StateOverride const& state : _states)
        states[state.tag] = state;
#define STATE_OVERRIDE(key) DynamicStateLookup<key>(states)

    std::vector<VkPipelineShaderStageCreateInfo> shader_stages{};
    shader_stages.reserve(_stages.size());

    for (uint32_t index = 0u; index < (uint32_t)_stages.size(); ++index)
    {
        VkPipelineShaderStageCreateInfo shader_stage{};
        shader_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shader_stage.pNext = nullptr;
        shader_stage.flags = 0u;
        shader_stage.stage = _stages[index].stage;
        shader_stage.module = _stages[index].module;
        shader_stage.pName = "main";
        shader_stage.pSpecializationInfo = nullptr;
        shader_stages.push_back(shader_stage);
    }

    VkPipelineRasterizationStateCreateInfo rasterizationState{};
    rasterizationState.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizationState.pNext = nullptr;
    rasterizationState.flags = 0u;
    rasterizationState.depthClampEnable = VK_FALSE;
    rasterizationState.rasterizerDiscardEnable = VK_FALSE;
    rasterizationState.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizationState.cullMode = STATE_OVERRIDE(StateOverrideTag::CullMode);
    rasterizationState.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizationState.depthBiasEnable = VK_FALSE;
    rasterizationState.depthBiasConstantFactor = 0.f;
    rasterizationState.depthBiasClamp = 0.f;
    rasterizationState.depthBiasSlopeFactor = 0.f;
    rasterizationState.lineWidth = 1.f;

    VkPipelineVertexInputStateCreateInfo vertexInputState{};
    vertexInputState.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputState.pNext = nullptr;
    vertexInputState.flags = 0u;
    vertexInputState.vertexBindingDescriptionCount = 0u;
    vertexInputState.pVertexBindingDescriptions = nullptr;
    vertexInputState.vertexAttributeDescriptionCount = 0u;
    vertexInputState.pVertexAttributeDescriptions = nullptr;

    std::vector<VkVertexInputBindingDescription> inputBindings = {};
    std::vector<VkVertexInputAttributeDescription> inputAttributes = {};
    for (VertexInput const& input : _input)
    {
        VkVertexInputBindingDescription binding = {};
        binding.binding = input.binding;
        binding.stride = input.stride;
        binding.inputRate = input.rate;

        VkVertexInputAttributeDescription attribute = {};
        attribute.location = input.location;
        attribute.binding = input.binding;
        attribute.format = input.format;
        attribute.offset = input.offset;

        inputBindings.push_back(binding);
        inputAttributes.push_back(attribute);
    }

    if (!_input.empty())
    {
        vertexInputState.vertexBindingDescriptionCount = (uint32_t)_input.size();
        vertexInputState.pVertexBindingDescriptions = inputBindings.data();
        vertexInputState.vertexAttributeDescriptionCount = (uint32_t)_input.size();
        vertexInputState.pVertexAttributeDescriptions = inputAttributes.data();
    }

    VkPipelineInputAssemblyStateCreateInfo inputAssemblyState{};
    inputAssemblyState.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssemblyState.pNext = nullptr;
    inputAssemblyState.flags = 0u;
    inputAssemblyState.topology = _topology;
    inputAssemblyState.primitiveRestartEnable = VK_FALSE;

    VkDynamicState dynamic_states[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.pNext = nullptr;
    dynamicState.flags = 0u;
    dynamicState.dynamicStateCount = 2u;
    dynamicState.pDynamicStates = dynamic_states;

    std::vector<VkPipelineColorBlendAttachmentState> blendAttachmentStates{};
    blendAttachmentStates.reserve(_blend.size());
    for (BlendState const& state : _blend)
    {
        blendAttachmentStates.push_back(VkPipelineColorBlendAttachmentState{
            state.blend_enable,
            state.src_blend_factor,
            state.dst_blend_factor,
            state.blend_op,
            state.src_blend_factor,
            state.dst_blend_factor,
            state.blend_op,
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
            VK_COLOR_COMPONENT_A_BIT
        });
    }
    if (blendAttachmentStates.empty())
    {
        VkPipelineColorBlendAttachmentState default_blend = {};
        default_blend.blendEnable = VK_FALSE;
        default_blend.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
            VK_COLOR_COMPONENT_A_BIT;
        blendAttachmentStates.push_back(default_blend);
    }

    VkPipelineColorBlendStateCreateInfo blendState{};
    blendState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blendState.pNext = nullptr;
    blendState.flags = 0;
    blendState.logicOpEnable = VK_FALSE;
    blendState.logicOp = VK_LOGIC_OP_CLEAR;
    blendState.attachmentCount = blendAttachmentStates.size();
    blendState.pAttachments = blendAttachmentStates.data();
    blendState.blendConstants[0] = 0;
    blendState.blendConstants[1] = 0;
    blendState.blendConstants[2] = 0;
    blendState.blendConstants[3] = 0;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.pNext = nullptr;
    viewportState.flags = 0u;
    viewportState.viewportCount = 1;
    viewportState.pViewports = nullptr;
    viewportState.scissorCount = 1;
    viewportState.pScissors = nullptr;

    VkPipelineMultisampleStateCreateInfo multisampleState{};
    multisampleState.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampleState.pNext = nullptr;
    multisampleState.flags = 0;
    multisampleState.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisampleState.sampleShadingEnable = VK_FALSE;
    multisampleState.minSampleShading = 0.f;
    multisampleState.pSampleMask = nullptr;
    multisampleState.alphaToCoverageEnable = VK_FALSE;
    multisampleState.alphaToOneEnable = VK_FALSE;

    VkPipelineDepthStencilStateCreateInfo depthStencilState{};
    depthStencilState.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencilState.pNext = nullptr;
    depthStencilState.flags = 0;
    depthStencilState.depthTestEnable = STATE_OVERRIDE(StateOverrideTag::DepthTestEnable);
    depthStencilState.depthWriteEnable = STATE_OVERRIDE(StateOverrideTag::DepthWriteEnable);
    depthStencilState.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencilState.depthBoundsTestEnable = VK_FALSE;
    depthStencilState.stencilTestEnable = VK_FALSE;
    depthStencilState.front = VkStencilOpState{};
    depthStencilState.back = VkStencilOpState{};
    depthStencilState.minDepthBounds = 0.f;
    depthStencilState.maxDepthBounds = 1.f;

    VkGraphicsPipelineCreateInfo createPipeline{};
    createPipeline.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    createPipeline.pNext = nullptr;
    createPipeline.flags = 0u;
    createPipeline.stageCount = (uint32_t)shader_stages.size();
    createPipeline.pStages = shader_stages.data();
    createPipeline.pVertexInputState = &vertexInputState;
    createPipeline.pInputAssemblyState = &inputAssemblyState;
    createPipeline.pTessellationState = nullptr;
    createPipeline.pViewportState = &viewportState;
    createPipeline.pRasterizationState = &rasterizationState;
    createPipeline.pMultisampleState = &multisampleState;
    createPipeline.pDepthStencilState = &depthStencilState;
    createPipeline.pColorBlendState = &blendState;
    createPipeline.pDynamicState = &dynamicState;
    createPipeline.layout = _layout;
    createPipeline.renderPass = _renderPass;
    createPipeline.subpass = 0;
    createPipeline.basePipelineHandle = 0;
    createPipeline.basePipelineIndex = 0;

    VkPipeline graphics_pipeline{};
    CHECKCALL(vkCreateGraphicsPipelines, device, VK_NULL_HANDLE,
              1u, &createPipeline, nullptr, &graphics_pipeline);

    return graphics_pipeline;

#undef STATE_OVERRIDE
}

Swapchain Context::CreateSwapchain(uint32_t const _size[2])
{
    Swapchain swapchain{};
    swapchain.size.width = _size[0];
    swapchain.size.height = _size[1];
    swapchain.vulkan = this;
    swapchain.format = VK_FORMAT_B8G8R8A8_SRGB;
    VkColorSpaceKHR color_space = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;

#if 0
    VkColorSpaceKHR color_space{};
    {
        std::uint32_t format_count = 1;
        VkSurfaceFormatKHR surface_format{};
        fp.GetPhysicalDeviceSurfaceFormatsKHR(physical_device,
                                              surface,
                                              &format_count, &surface_format);

        swapchain.format = surface_format.format;
        if (surface_format.format == VK_FORMAT_UNDEFINED)
            swapchain.format = VK_FORMAT_B8G8R8A8_UNORM;

        color_space = surface_format.colorSpace;
    }
#endif

    RenderPassAttachment depth_attachment =
        { VK_FORMAT_D32_SFLOAT,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

    swapchain.render_pass = CreateRenderPass(
        {
            { swapchain.format,
              VK_IMAGE_LAYOUT_UNDEFINED,
              VK_IMAGE_LAYOUT_PRESENT_SRC_KHR }
        },
        &depth_attachment
        );

    VkSurfaceCapabilitiesKHR surface_capabilities{};
    fp.GetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device,
                                               surface,
                                               &surface_capabilities);

    VkExtent2D swapchain_extent = surface_capabilities.currentExtent;
    if (surface_capabilities.currentExtent.width == (uint32_t)~0u)
        swapchain_extent = surface_capabilities.minImageExtent;

    VkSwapchainCreateInfoKHR swapchain_info{};
    swapchain_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchain_info.pNext = nullptr;
    swapchain_info.flags = 0;
    swapchain_info.surface = surface;
    swapchain_info.minImageCount = 2;
    swapchain_info.imageFormat = swapchain.format;
    swapchain_info.imageColorSpace = color_space;
    swapchain_info.imageExtent = swapchain_extent;
    swapchain_info.imageArrayLayers = 1;
    swapchain_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
        | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    swapchain_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchain_info.queueFamilyIndexCount = 0;
    swapchain_info.pQueueFamilyIndices = nullptr;
    swapchain_info.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    swapchain_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchain_info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    swapchain_info.clipped = VK_TRUE;
    swapchain_info.oldSwapchain = VK_NULL_HANDLE;

    CHECKCALL(fp.CreateSwapchainKHR, device, &swapchain_info, nullptr, &swapchain.handle);

    uint32_t image_count = 0u;
    fp.GetSwapchainImagesKHR(device, swapchain.handle, &image_count,
                             nullptr);
    swapchain.images.resize(image_count);
    swapchain.views.resize(image_count*2);
    swapchain.depth_buffers.resize(image_count);
    swapchain.framebuffers.resize(image_count);
    fp.GetSwapchainImagesKHR(device, swapchain.handle, &image_count,
                             swapchain.images.data());

    for (uint32_t index = 0u; index < image_count; ++index)
    {
        {
            VkImageViewCreateInfo image_view_info{};
            image_view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            image_view_info.pNext = nullptr;
            image_view_info.flags = 0;
            image_view_info.image = swapchain.images[index];
            image_view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
            image_view_info.format = swapchain.format;
            image_view_info.components = {
                VK_COMPONENT_SWIZZLE_R,
                VK_COMPONENT_SWIZZLE_G,
                VK_COMPONENT_SWIZZLE_B,
                VK_COMPONENT_SWIZZLE_A
            };
            image_view_info.subresourceRange = {
                VK_IMAGE_ASPECT_COLOR_BIT,
                0, 1, 0, 1
            };

            CHECKCALL(vkCreateImageView, device, &image_view_info, nullptr,
                      &swapchain.views[index * 2]);
        }

        swapchain.depth_buffers[index] =
            CreateTexture(_size[0], _size[1], 1,
                          VK_IMAGE_TYPE_2D, VK_FORMAT_D32_SFLOAT,
                          VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);

        {
            VkImageViewCreateInfo image_view_info{};
            image_view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            image_view_info.pNext = nullptr;
            image_view_info.flags = 0;
            image_view_info.image = swapchain.depth_buffers[index].image;
            image_view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
            image_view_info.format = VK_FORMAT_D32_SFLOAT;
            image_view_info.components = {
                VK_COMPONENT_SWIZZLE_R,
                VK_COMPONENT_SWIZZLE_G,
                VK_COMPONENT_SWIZZLE_B,
                VK_COMPONENT_SWIZZLE_A
            };
            image_view_info.subresourceRange = {
                VK_IMAGE_ASPECT_DEPTH_BIT,
                0, 1, 0, 1
            };

            CHECKCALL(vkCreateImageView, device, &image_view_info, nullptr,
                      &swapchain.views[index*2 + 1]);
        }

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.pNext = nullptr;
        framebufferInfo.flags = 0u;
        framebufferInfo.renderPass = swapchain.render_pass;
        framebufferInfo.attachmentCount = 2u;
        framebufferInfo.pAttachments = &swapchain.views[index * 2];
        framebufferInfo.width = _size[0];
        framebufferInfo.height = _size[1];
        framebufferInfo.layers = 1;

        CHECKCALL(vkCreateFramebuffer, device, &framebufferInfo, nullptr,
                  &swapchain.framebuffers[index]);
    }

    return swapchain;
}

DescriptorPool Context::CreateDescriptorPool(
    uint32_t _max_sets,
    std::vector<DescriptorSetBindingPoint> const& _bindings)
{
    DescriptorPool output = {};

    std::unordered_map<VkDescriptorType, uint32_t> counts = {};
    std::vector<VkDescriptorSetLayoutBinding> bindings = {};
    std::vector<VkDescriptorBindingFlags> flags = {};

    for (DescriptorSetBindingPoint const& binding : _bindings)
    {
        if (counts.count(binding.type) == 0)
            counts.emplace(binding.type, 0u);
        counts[binding.type] += binding.count;

        VkDescriptorSetLayoutBinding layout_binding = {};
        layout_binding.binding = bindings.size();
        layout_binding.descriptorType = binding.type;
        layout_binding.descriptorCount = binding.count;
        layout_binding.stageFlags = binding.shader_stages;
        layout_binding.pImmutableSamplers = nullptr;
        bindings.emplace_back(layout_binding);
        flags.emplace_back(binding.flags);
    }

    std::vector<VkDescriptorPoolSize> descpool_sizes = {};
    for (auto const& count : counts)
        descpool_sizes.emplace_back(VkDescriptorPoolSize{ count.first, count.second * _max_sets });

    VkDescriptorPoolCreateInfo descpool_create = {};
    descpool_create.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descpool_create.pNext = nullptr;
    descpool_create.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    descpool_create.maxSets = _max_sets;
    descpool_create.poolSizeCount = (uint32_t)descpool_sizes.size();
    descpool_create.pPoolSizes = descpool_sizes.data();
    CHECKCALL(vkCreateDescriptorPool, device,
              &descpool_create, nullptr, &output.pool);

    VkDescriptorSetLayoutBindingFlagsCreateInfo flags_info{};
    flags_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
    flags_info.pNext = nullptr;
    flags_info.bindingCount = (uint32_t)flags.size();
    flags_info.pBindingFlags = flags.data();

    VkDescriptorSetLayoutCreateInfo layout_create{};
    layout_create.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_create.pNext = &flags_info;
    layout_create.flags = 0u;
    layout_create.bindingCount = (uint32_t)bindings.size();
    layout_create.pBindings = bindings.data();
    CHECKCALL(vkCreateDescriptorSetLayout, device,
              &layout_create, nullptr, &output.layout);

    return output;
}

void Context::DestroyDescriptorPool(DescriptorPool const& _pool) const
{
    vkDestroyDescriptorPool(device, _pool.pool, nullptr);
    vkDestroyDescriptorSetLayout(device, _pool.layout, nullptr);
}

DynamicDescriptorPool Context::CreateDynamicDescriptorPool(
    std::vector<DescriptorSetBindingPoint> const& _bindings)
{
    DynamicDescriptorPool output = {};
    output.context = this;

    std::unordered_map<VkDescriptorType, uint32_t> counts = {};
    std::vector<VkDescriptorSetLayoutBinding> bindings = {};
    std::vector<VkDescriptorBindingFlags> flags = {};

    for (DescriptorSetBindingPoint const& binding : _bindings)
    {
        if (counts.count(binding.type) == 0)
            counts.emplace(binding.type, 0u);
        counts[binding.type] += binding.count;

        VkDescriptorSetLayoutBinding layout_binding = {};
        layout_binding.binding = bindings.size();
        layout_binding.descriptorType = binding.type;
        layout_binding.descriptorCount = binding.count;
        layout_binding.stageFlags = binding.shader_stages;
        layout_binding.pImmutableSamplers = nullptr;
        bindings.emplace_back(layout_binding);
        flags.emplace_back(binding.flags);
    }

    output.sizes.clear();
    for (auto const& count : counts)
        output.sizes.push_back(VkDescriptorPoolSize{ count.first, count.second * DynamicDescriptorPool::kPoolSize });

    VkDescriptorSetLayoutBindingFlagsCreateInfo flags_info{};
    flags_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
    flags_info.pNext = nullptr;
    flags_info.bindingCount = (uint32_t)flags.size();
    flags_info.pBindingFlags = flags.data();

    VkDescriptorSetLayoutCreateInfo layout_create{};
    layout_create.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_create.pNext = &flags_info;
    layout_create.flags = 0u;
    layout_create.bindingCount = (uint32_t)bindings.size();
    layout_create.pBindings = bindings.data();
    CHECKCALL(vkCreateDescriptorSetLayout, device,
              &layout_create, nullptr, &output.layout);

    return output;
}

void Context::DestroyDynamicDescriptorPool(DynamicDescriptorPool const& _pool) const
{
    for (VkDescriptorPool pool : _pool.pools)
        vkDestroyDescriptorPool(device, pool, nullptr);
    vkDestroyDescriptorSetLayout(device, _pool.layout, nullptr);
}

VkDescriptorSet DynamicDescriptorPool::AllocateDescriptorSet(void const* _pNext)
{
    if (total_set_count >= pools.size() * kPoolSize)
    {
        VkDescriptorPool pool = {};

        VkDescriptorPoolCreateInfo descpool_create = {};
        descpool_create.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        descpool_create.pNext = nullptr;
        descpool_create.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        descpool_create.maxSets = kPoolSize;
        descpool_create.poolSizeCount = (uint32_t)sizes.size();
        descpool_create.pPoolSizes = sizes.data();
        CHECKCALL(vkCreateDescriptorPool, context->device,
                  &descpool_create, nullptr, &pool);

        first_free_pool = (uint32_t)pools.size();
        pools.push_back(pool);
        sets.resize(pools.size());
        set_counts.push_back(0);
    }

    VkDescriptorSet output = {};
    VkDescriptorSetAllocateInfo descset_allocate{};
    descset_allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    descset_allocate.pNext = _pNext;
    descset_allocate.descriptorPool = pools[first_free_pool];
    descset_allocate.descriptorSetCount = 1u;
    descset_allocate.pSetLayouts = &layout;
    CHECKCALL(vkAllocateDescriptorSets, context->device,
              &descset_allocate, &output);

    sets[first_free_pool].emplace(output);

    ++total_set_count;
    ++set_counts[first_free_pool];
    while (first_free_pool < (uint32_t)set_counts.size()
           && set_counts[first_free_pool] == kPoolSize)
        ++first_free_pool;

    return output;
}

void DynamicDescriptorPool::FreeDescriptorSet(VkDescriptorSet _set)
{
    for (uint32_t index = 0u; index < (uint32_t)sets.size(); ++index)
    {
        if (sets[index].count(_set))
        {
            sets[index].erase(_set);
            --set_counts[index];
            --total_set_count;
            if (index < first_free_pool)
                first_free_pool = index;
            vkFreeDescriptorSets(context->device, pools[index], 1, &_set);
            break;
        }
    }
}

Buffer Context::CreateBuffer(uint32_t _size,
                             VkBufferUsageFlags _usage,
                             VkMemoryPropertyFlags _memory_flags)
{
    VkBufferCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    create_info.pNext = nullptr;
    create_info.flags = 0;
    create_info.size = (VkDeviceSize)_size;
    create_info.usage = _usage;
    create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    create_info.queueFamilyIndexCount = 0;
    create_info.pQueueFamilyIndices = nullptr;

    Buffer buffer = {};
    CHECKCALL(vkCreateBuffer, device, &create_info, nullptr, &buffer.buffer);

    VkMemoryRequirements reqs = {};
    vkGetBufferMemoryRequirements(device, buffer.buffer, &reqs);

    buffer.alloc = allocator.Allocate(_memory_flags, _size, reqs.alignment);
    CHECKCALL(vkBindBufferMemory, device, buffer.buffer, buffer.alloc.memory, buffer.alloc.offset);

    return buffer;
}

Buffer Context::CreateBuffer(uint32_t _size,
                             VkBufferUsageFlags _usage,
                             std::function<void(void*)> const& _upload_callback,
                             VkMemoryPropertyFlags _memory_flags)
{
    Buffer result = CreateBuffer(_size, _usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, _memory_flags);

    Buffer staging = CreateBuffer(_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                                  | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    void* mapped_data = staging.alloc.map;
    _upload_callback(mapped_data);

    vktk::CommandBuffer command_buffer = CreateCommandBuffer(present_queue);
    command_buffer.Begin();

    command_buffer.BufferTransitions(
        VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        {
            { result, VK_ACCESS_NONE, VK_ACCESS_TRANSFER_WRITE_BIT },
            { staging, VK_ACCESS_HOST_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT }
        });

    VkBufferCopy copy_region{ 0, 0, _size };
    vkCmdCopyBuffer(command_buffer, staging, result, 1, &copy_region);
    command_buffer.End();

    VkFence staging_fence = VK_NULL_HANDLE;
    CommandBuffer::SubmitInfo submit_info = {};
    submit_info.signal_fence = &staging_fence;
    command_buffer.Submit(submit_info);

    ScheduleRoutine(staging_fence, [staging](Context& _context) {
#if 0
        std::cout << "[ScheduledRoutine] staging buffer destroyed "
                  << std::hex << staging.buffer << std::dec << std::endl;
#endif
        _context.DestroyBuffer(staging);
    });
    ScheduleFenceDestroy(staging_fence);

    return result;
}

void Context::DestroyBuffer(Buffer const& _buffer)
{
    if (!_buffer.buffer)
        return;

    vkDestroyBuffer(device, _buffer, nullptr);
    allocator.Free(_buffer.alloc);
}

Texture Context::CreateTexture(uint32_t _width, uint32_t _height, uint32_t _depth,
                               VkImageType _type, VkFormat _format, VkImageUsageFlags _usage)
{
    return CreateTexture(_width, _height, _depth, 1, _type, _format, _usage);
}

Texture Context::CreateTexture(uint32_t _width, uint32_t _height, uint32_t _depth,
                               uint32_t _mip_count,
                               VkImageType _type, VkFormat _format, VkImageUsageFlags _usage)
{
    VkImageCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    create_info.pNext = nullptr;
    create_info.flags = 0;
    create_info.imageType = _type;
    create_info.format = _format;
    create_info.extent = VkExtent3D{ _width, _height, _depth };
    create_info.mipLevels = _mip_count;
    create_info.arrayLayers = 1;
    create_info.samples = VK_SAMPLE_COUNT_1_BIT;
    create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    create_info.usage = _usage;
    create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    create_info.queueFamilyIndexCount = 0;
    create_info.pQueueFamilyIndices = nullptr;
    create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    Texture texture{};
    texture.image_type = _type;
    texture.format = _format;
    CHECKCALL(vkCreateImage, device, &create_info, nullptr, &texture.image);

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device, texture.image, &requirements);

    texture.alloc = allocator.Allocate(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, requirements.size, requirements.alignment);
    CHECKCALL(vkBindImageMemory, device, texture.image, texture.alloc.memory, texture.alloc.offset);

    texture.view = CreateImageView(texture);
    return texture;
}

Texture Context::CreateTexture(uint32_t _width, uint32_t _height, uint32_t _depth,
                               VkImageType _type, VkFormat _format, VkImageUsageFlags _usage,
                               std::function<void(void*)> const& _upload_callback)
{
    return CreateTexture(_width, _height, _depth,
                         _type, _format, _usage,
                         ImageSyncTriplet{
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             VK_ACCESS_SHADER_READ_BIT,
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                         },
                         _upload_callback);
}

Texture Context::CreateTexture(uint32_t _width, uint32_t _height, uint32_t _depth,
                               VkImageType _type, VkFormat _format, VkImageUsageFlags _usage,
                               ImageSyncTriplet const& _sync,
                               std::function<void(void*)> const& _upload_callback)
{
    DeferredUpload upload = {};
    Texture result = CreateTexture(_width, _height, _depth, _type, _format,
                                   _usage | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                   _sync, upload);

    void* mapped_data = upload.staging.alloc.map;
    _upload_callback(mapped_data);

    CommitStaging(upload);

    return result;
}


Texture Context::CreateTexture(uint32_t _width, uint32_t _height, uint32_t _depth,
                               VkImageType _type, VkFormat _format, VkImageUsageFlags _usage,
                               ImageSyncTriplet const& _sync,
                               DeferredUpload& _upload)
{
    Texture result = CreateTexture(_width, _height, _depth, _type, _format,
                                   _usage | VK_IMAGE_USAGE_TRANSFER_DST_BIT);

    uint64_t staging_size = _width * _height * _depth * VkFormatBytesPerUnit(_format);
    _upload.staging = CreateBuffer(staging_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);

    VkImageMemoryBarrier src_memory_barrier{};
    src_memory_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    src_memory_barrier.pNext = nullptr;
    src_memory_barrier.srcAccessMask = VK_ACCESS_NONE;
    src_memory_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    src_memory_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    src_memory_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    src_memory_barrier.srcQueueFamilyIndex = 0u;
    src_memory_barrier.dstQueueFamilyIndex = 0u;
    src_memory_barrier.image = result.image;
    src_memory_barrier.subresourceRange = VkImageSubresourceRange{
        VK_IMAGE_ASPECT_COLOR_BIT,
        0u, 1u, 0u, 1u
    };

    VkBufferImageCopy copy_region{};
    copy_region.bufferOffset = 0u;
    copy_region.bufferRowLength = 0u;
    copy_region.bufferImageHeight = 0u;
    copy_region.imageSubresource = VkImageSubresourceLayers {
        VK_IMAGE_ASPECT_COLOR_BIT,
        0u, 0u, 1u
    };
    copy_region.imageOffset = VkOffset3D{ 0u, 0u, 0u };
    copy_region.imageExtent = VkExtent3D{ _width, _height, _depth };

    VkImageMemoryBarrier dst_memory_barrier = src_memory_barrier;
    dst_memory_barrier.oldLayout = dst_memory_barrier.newLayout;
    dst_memory_barrier.newLayout = _sync.layout;
    dst_memory_barrier.srcAccessMask = dst_memory_barrier.dstAccessMask;
    dst_memory_barrier.dstAccessMask = _sync.access;

    _upload.copy_command = [src_memory_barrier,
                            copy_region,
                            dst_memory_barrier,
                            sync_stage = _sync.stage,
                            buffer = _upload.staging,
                            image = result.image]
        (vktk::CommandBuffer& command_buffer) {

        vkCmdPipelineBarrier(command_buffer,
                             VK_PIPELINE_STAGE_HOST_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0u,
                             0u, nullptr,
                             0u, nullptr,
                             1u, &src_memory_barrier);

        vkCmdCopyBufferToImage(command_buffer,
                               buffer,
                               image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               1,
                               &copy_region);

        vkCmdPipelineBarrier(command_buffer,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             sync_stage,
                             0u,
                             0u, nullptr,
                             0u, nullptr,
                             1u, &dst_memory_barrier);
    };

    return result;
}


void Context::DestroyTexture(Texture const& _texture)
{
    vkDestroyImageView(device, _texture.view, nullptr);
    vkDestroyImage(device, _texture.image, nullptr);
}

void Context::CommitStaging(DeferredUpload& _upload)
{
    vktk::CommandBuffer command_buffer = CreateCommandBuffer(present_queue);
    command_buffer.Begin();

    VkBufferMemoryBarrier buffer_memory_barrier{};
    buffer_memory_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    buffer_memory_barrier.pNext = nullptr;
    buffer_memory_barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
    buffer_memory_barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    buffer_memory_barrier.srcQueueFamilyIndex = 0u;
    buffer_memory_barrier.dstQueueFamilyIndex = 0u;
    buffer_memory_barrier.buffer = _upload.staging.buffer;
    buffer_memory_barrier.offset = 0u;
    buffer_memory_barrier.size = _upload.staging.alloc.size;

    vkCmdPipelineBarrier(command_buffer,
                         VK_PIPELINE_STAGE_HOST_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0u,
                         0u, nullptr,
                         1u, &buffer_memory_barrier,
                         0u, nullptr);

    _upload.copy_command(command_buffer);

    command_buffer.End();

    VkFence staging_fence = VK_NULL_HANDLE;
    CommandBuffer::SubmitInfo submit_info = {};
    submit_info.signal_fence = &staging_fence;
    command_buffer.Submit(submit_info);

#if 0
    std::cout << "[CreateTexture] scheduled staging buffer destruction "
              << std::hex << staging.buffer << std::dec << std::endl;
#endif

    ScheduleRoutine(staging_fence, [staging = _upload.staging](Context& _context) {
#if 0
        std::cout << "[ScheduledRoutine] staging buffer destroyed "
                  << std::hex << staging.buffer << std::dec << std::endl;
#endif
        _context.DestroyBuffer(staging);
    });

    ScheduleFenceDestroy(staging_fence);

    _upload = DeferredUpload{};
}

VkImageView Context::CreateImageView(Texture const& _texture) const
{
    VkImageAspectFlags view_aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    switch (_texture.format)
    {
    case VK_FORMAT_D16_UNORM:
    case VK_FORMAT_X8_D24_UNORM_PACK32:
    case VK_FORMAT_D32_SFLOAT:
        view_aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
        break;
    case VK_FORMAT_S8_UINT:
        view_aspect = VK_IMAGE_ASPECT_STENCIL_BIT;
        break;
    case VK_FORMAT_D16_UNORM_S8_UINT:
    case VK_FORMAT_D24_UNORM_S8_UINT:
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
        view_aspect = VK_IMAGE_ASPECT_STENCIL_BIT | VK_IMAGE_ASPECT_DEPTH_BIT;
        break;
    default: break;
    }

    VkImageView output = {};
    VkImageViewCreateInfo image_view_create{};
    image_view_create.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    image_view_create.pNext = nullptr;
    image_view_create.flags = 0u;
    image_view_create.image = _texture.image;
    image_view_create.viewType = (VkImageViewType)_texture.image_type;
    image_view_create.format = _texture.format;
    image_view_create.components = VkComponentMapping{};
    image_view_create.subresourceRange = VkImageSubresourceRange{
        view_aspect,
        0u, VK_REMAINING_MIP_LEVELS, 0u, 1u
    };

    CHECKCALL(vkCreateImageView, device,
              &image_view_create, nullptr, &output);

    return output;
}

VkFramebuffer Swapchain::AcquireNext(VkSemaphore _signal)
{
    CHECKCALL(vulkan->fp.AcquireNextImageKHR,
              vulkan->device,
              handle,
              UINT64_MAX,
              _signal,
              VK_NULL_HANDLE,
              &index);

    return framebuffers[index];
}

void Swapchain::Release()
{
    for (VkImageView view : views)
        vkDestroyImageView(vulkan->device, view, nullptr);
    for (VkFramebuffer framebuffer : framebuffers)
        vkDestroyFramebuffer(vulkan->device, framebuffer, nullptr);
    for (Texture& texture : depth_buffers)
        vulkan->DestroyTexture(texture);
    vkDestroyRenderPass(vulkan->device, render_pass, nullptr);

    vulkan->fp.DestroySwapchainKHR(vulkan->device, handle, nullptr);

    handle = VK_NULL_HANDLE;
}

uint32_t Context::SelectMemoryType(VkMemoryPropertyFlags _flags, uint32_t _required_size)
{
    for (uint32_t index = 0u; index < memory_properties.memoryTypeCount; ++index)
    {
        VkMemoryType const& memory_type = memory_properties.memoryTypes[index];
        VkMemoryHeap const& memory_heap = memory_properties.memoryHeaps[memory_type.heapIndex];
        if ((memory_type.propertyFlags & _flags) == _flags
            && memory_heap.size >= _required_size)
        {
            return index;
        }
    }
    return ~0u;
}

VkDeviceMemory Context::MemAlloc(VkMemoryPropertyFlags _flags, uint32_t _size)
{
    VkMemoryAllocateInfo allocate_info{};
    allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate_info.pNext = nullptr;
    allocate_info.allocationSize = _size;
    allocate_info.memoryTypeIndex = SelectMemoryType(_flags, _size);
    VkDeviceMemory device_memory = {};
    CHECKCALL(vkAllocateMemory, device, &allocate_info, nullptr, &device_memory);
    return device_memory;
}

void Context::ScheduleRoutine(VkFence _fence, SynchronisedRoutine _routine)
{
    if (!pending_routines.contains(_fence))
        pending_routines.emplace(_fence, std::vector<SynchronisedRoutine>{});
    pending_routines[_fence].push_back(_routine);
}

void Context::ScheduleFenceDestroy(VkFence _fence)
{
    assert(std::find(pending_fences.begin(), pending_fences.end(), _fence) == pending_fences.end());
    pending_fences.emplace(_fence);
}

void Context::TryScheduledRoutines()
{
    std::vector<VkFence> finished_fences = {};
    for (auto const& bind_point : pending_routines)
    {
        if (vkGetFenceStatus(device, bind_point.first) != VK_SUCCESS)
            continue;

        for (SynchronisedRoutine const& routine : bind_point.second)
            routine(*this);

        finished_fences.push_back(bind_point.first);
    }

    for (VkFence fence : finished_fences)
        pending_routines.erase(fence);
    finished_fences.clear();

    for (VkFence fence : pending_fences)
    {
        if (vkGetFenceStatus(device, fence) != VK_SUCCESS)
            continue;

        fence_pool.ResetFence(*this, fence);
        --fence_count;
#if 0
        std::cout << "[PollFences] destroying fence " << std::hex
                  << fence << std::dec << std::endl;
#endif
        finished_fences.push_back(fence);
    }

    for (VkFence fence : finished_fences)
        pending_fences.erase(fence);
}

void Context::WaitScheduledRoutines()
{
    for (auto const& bind_point : pending_routines)
    {
        CHECKCALL(vkWaitForFences, device, 1, &bind_point.first, VK_TRUE, ~0ull);

        for (SynchronisedRoutine const& routine : bind_point.second)
            routine(*this);
    }
    pending_routines.clear();

    for (VkFence fence : pending_fences)
    {
        CHECKCALL(vkWaitForFences, device, 1, &fence, VK_TRUE, ~0ull);
        fence_pool.ResetFence(*this, fence);
        --fence_count;
        std::cout << "[WaitFences] destroying fence " << std::hex
                  << fence << std::dec << std::endl;
    }
    pending_fences.clear();
}

char const* VkResultToStr(VkResult _vkResult)
{
    switch(_vkResult)
    {
    case VK_SUCCESS: return "VK_SUCCESS";
    case VK_NOT_READY: return "VK_NOT_READY";
    case VK_TIMEOUT: return "VK_TIMEOUT";
    case VK_EVENT_SET: return "VK_EVENT_SET";
    case VK_EVENT_RESET: return "VK_EVENT_RESET";
    case VK_INCOMPLETE: return "VK_INCOMPLETE";
    case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
    case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
    case VK_ERROR_TOO_MANY_OBJECTS: return "VK_ERROR_TOO_MANY_OBJECTS";
    case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
    case VK_ERROR_FRAGMENTED_POOL: return "VK_ERROR_FRAGMENTED_POOL";
    case VK_ERROR_UNKNOWN: return "VK_ERROR_UNKNOWN";
    case VK_ERROR_OUT_OF_POOL_MEMORY: return "VK_ERROR_OUT_OF_POOL_MEMORY";
    case VK_ERROR_INVALID_EXTERNAL_HANDLE: return "VK_ERROR_INVALID_EXTERNAL_HANDLE";
    case VK_ERROR_FRAGMENTATION: return "VK_ERROR_FRAGMENTATION";
    case VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS: return "VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS";
    case VK_PIPELINE_COMPILE_REQUIRED: return "VK_PIPELINE_COMPILE_REQUIRED";
    case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
    case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR: return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
    case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
    case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
    case VK_ERROR_INCOMPATIBLE_DISPLAY_KHR: return "VK_ERROR_INCOMPATIBLE_DISPLAY_KHR";
    case VK_ERROR_VALIDATION_FAILED_EXT: return "VK_ERROR_VALIDATION_FAILED_EXT";
    case VK_ERROR_INVALID_SHADER_NV: return "VK_ERROR_INVALID_SHADER_NV";
    case VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT: return "VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT";
    case VK_ERROR_NOT_PERMITTED_KHR: return "VK_ERROR_NOT_PERMITTED_KHR";
    case VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT: return "VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT";
    case VK_THREAD_IDLE_KHR: return "VK_THREAD_IDLE_KHR";
    case VK_THREAD_DONE_KHR: return "VK_THREAD_DONE_KHR";
    case VK_OPERATION_DEFERRED_KHR: return "VK_OPERATION_DEFERRED_KHR";
    case VK_OPERATION_NOT_DEFERRED_KHR: return "VK_OPERATION_NOT_DEFERRED_KHR";
    default: return "";
    }
}

uint32_t VkFormatBytesPerUnit(VkFormat _format)
{
    switch(_format)
    {
    case VK_FORMAT_R4G4_UNORM_PACK8:
    case VK_FORMAT_R8_UNORM:
    case VK_FORMAT_R8_SNORM:
    case VK_FORMAT_R8_USCALED:
    case VK_FORMAT_R8_SSCALED:
    case VK_FORMAT_R8_UINT:
    case VK_FORMAT_R8_SINT:
    case VK_FORMAT_R8_SRGB:
    case VK_FORMAT_S8_UINT:
        return 1;

    case VK_FORMAT_R4G4B4A4_UNORM_PACK16:
    case VK_FORMAT_B4G4R4A4_UNORM_PACK16:
    case VK_FORMAT_R5G6B5_UNORM_PACK16:
    case VK_FORMAT_B5G6R5_UNORM_PACK16:
    case VK_FORMAT_R5G5B5A1_UNORM_PACK16:
    case VK_FORMAT_B5G5R5A1_UNORM_PACK16:
    case VK_FORMAT_A1R5G5B5_UNORM_PACK16:
    case VK_FORMAT_R8G8_UNORM:
    case VK_FORMAT_R8G8_SNORM:
    case VK_FORMAT_R8G8_USCALED:
    case VK_FORMAT_R8G8_SSCALED:
    case VK_FORMAT_R8G8_UINT:
    case VK_FORMAT_R8G8_SINT:
    case VK_FORMAT_R8G8_SRGB:
    case VK_FORMAT_R16_UNORM:
    case VK_FORMAT_R16_SNORM:
    case VK_FORMAT_R16_USCALED:
    case VK_FORMAT_R16_SSCALED:
    case VK_FORMAT_R16_UINT:
    case VK_FORMAT_R16_SINT:
    case VK_FORMAT_R16_SFLOAT:
    case VK_FORMAT_D16_UNORM:
    case VK_FORMAT_D16_UNORM_S8_UINT:
        return 2;

    case VK_FORMAT_R8G8B8_UNORM:
    case VK_FORMAT_R8G8B8_SNORM:
    case VK_FORMAT_R8G8B8_USCALED:
    case VK_FORMAT_R8G8B8_SSCALED:
    case VK_FORMAT_R8G8B8_UINT:
    case VK_FORMAT_R8G8B8_SINT:
    case VK_FORMAT_R8G8B8_SRGB:
    case VK_FORMAT_B8G8R8_UNORM:
    case VK_FORMAT_B8G8R8_SNORM:
    case VK_FORMAT_B8G8R8_USCALED:
    case VK_FORMAT_B8G8R8_SSCALED:
    case VK_FORMAT_B8G8R8_UINT:
    case VK_FORMAT_B8G8R8_SINT:
    case VK_FORMAT_B8G8R8_SRGB:
    case VK_FORMAT_D24_UNORM_S8_UINT:
        return 3;

    case VK_FORMAT_R8G8B8A8_UNORM:
    case VK_FORMAT_R8G8B8A8_SNORM:
    case VK_FORMAT_R8G8B8A8_USCALED:
    case VK_FORMAT_R8G8B8A8_SSCALED:
    case VK_FORMAT_R8G8B8A8_UINT:
    case VK_FORMAT_R8G8B8A8_SINT:
    case VK_FORMAT_R8G8B8A8_SRGB:
    case VK_FORMAT_B8G8R8A8_UNORM:
    case VK_FORMAT_B8G8R8A8_SNORM:
    case VK_FORMAT_B8G8R8A8_USCALED:
    case VK_FORMAT_B8G8R8A8_SSCALED:
    case VK_FORMAT_B8G8R8A8_UINT:
    case VK_FORMAT_B8G8R8A8_SINT:
    case VK_FORMAT_B8G8R8A8_SRGB:
    case VK_FORMAT_A8B8G8R8_UNORM_PACK32:
    case VK_FORMAT_A8B8G8R8_SNORM_PACK32:
    case VK_FORMAT_A8B8G8R8_USCALED_PACK32:
    case VK_FORMAT_A8B8G8R8_SSCALED_PACK32:
    case VK_FORMAT_A8B8G8R8_UINT_PACK32:
    case VK_FORMAT_A8B8G8R8_SINT_PACK32:
    case VK_FORMAT_A8B8G8R8_SRGB_PACK32:
    case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
    case VK_FORMAT_A2R10G10B10_SNORM_PACK32:
    case VK_FORMAT_A2R10G10B10_USCALED_PACK32:
    case VK_FORMAT_A2R10G10B10_SSCALED_PACK32:
    case VK_FORMAT_A2R10G10B10_UINT_PACK32:
    case VK_FORMAT_A2R10G10B10_SINT_PACK32:
    case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
    case VK_FORMAT_A2B10G10R10_SNORM_PACK32:
    case VK_FORMAT_A2B10G10R10_USCALED_PACK32:
    case VK_FORMAT_A2B10G10R10_SSCALED_PACK32:
    case VK_FORMAT_A2B10G10R10_UINT_PACK32:
    case VK_FORMAT_A2B10G10R10_SINT_PACK32:
    case VK_FORMAT_R16G16_UNORM:
    case VK_FORMAT_R16G16_SNORM:
    case VK_FORMAT_R16G16_USCALED:
    case VK_FORMAT_R16G16_SSCALED:
    case VK_FORMAT_R16G16_UINT:
    case VK_FORMAT_R16G16_SINT:
    case VK_FORMAT_R16G16_SFLOAT:
    case VK_FORMAT_R32_UINT:
    case VK_FORMAT_R32_SINT:
    case VK_FORMAT_R32_SFLOAT:
    case VK_FORMAT_B10G11R11_UFLOAT_PACK32:
    case VK_FORMAT_E5B9G9R9_UFLOAT_PACK32:
    case VK_FORMAT_X8_D24_UNORM_PACK32:
    case VK_FORMAT_D32_SFLOAT:
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
        return 4;

    case VK_FORMAT_R16G16B16_UNORM:
    case VK_FORMAT_R16G16B16_SNORM:
    case VK_FORMAT_R16G16B16_USCALED:
    case VK_FORMAT_R16G16B16_SSCALED:
    case VK_FORMAT_R16G16B16_UINT:
    case VK_FORMAT_R16G16B16_SINT:
    case VK_FORMAT_R16G16B16_SFLOAT:
        return 6;

    case VK_FORMAT_R16G16B16A16_UNORM:
    case VK_FORMAT_R16G16B16A16_SNORM:
    case VK_FORMAT_R16G16B16A16_USCALED:
    case VK_FORMAT_R16G16B16A16_SSCALED:
    case VK_FORMAT_R16G16B16A16_UINT:
    case VK_FORMAT_R16G16B16A16_SINT:
    case VK_FORMAT_R16G16B16A16_SFLOAT:
    case VK_FORMAT_R32G32_UINT:
    case VK_FORMAT_R32G32_SINT:
    case VK_FORMAT_R32G32_SFLOAT:
    case VK_FORMAT_R64_UINT:
    case VK_FORMAT_R64_SINT:
    case VK_FORMAT_R64_SFLOAT:
        return 8;

    case VK_FORMAT_R32G32B32_UINT:
    case VK_FORMAT_R32G32B32_SINT:
    case VK_FORMAT_R32G32B32_SFLOAT:
        return 12;

    case VK_FORMAT_R32G32B32A32_UINT:
    case VK_FORMAT_R32G32B32A32_SINT:
    case VK_FORMAT_R32G32B32A32_SFLOAT:
    case VK_FORMAT_R64G64_UINT:
    case VK_FORMAT_R64G64_SINT:
    case VK_FORMAT_R64G64_SFLOAT:
        return 16;

    case VK_FORMAT_R64G64B64_UINT:
    case VK_FORMAT_R64G64B64_SINT:
    case VK_FORMAT_R64G64B64_SFLOAT:
        return 24;

    case VK_FORMAT_R64G64B64A64_UINT:
    case VK_FORMAT_R64G64B64A64_SINT:
    case VK_FORMAT_R64G64B64A64_SFLOAT:
        return 32;

    default:
        return 0;
    }
}

void* PNextLookup(VkBaseOutStructure* _out_struct, VkStructureType _type)
{
    while (_out_struct)
    {
        if (_out_struct->sType == _type)
            return _out_struct;
        _out_struct = _out_struct->pNext;
    }
    return nullptr;
}

} // namespace vktk

namespace vkrg
{

namespace runtime
{

void
DispatchContext::ClearStaticRuntime()
{
    ClearDynamicRuntime();

    graph.DepthFirstTraversal([this](uint32_t, dstk::Registry::Node node) {
        auto const* dispatch_stage = registry.ComponentLookup<layout::DispatchStage>(node);
        if (dispatch_stage)
        {
            auto* stage_static = registry.ComponentLookup<DispatchStageStatic>(node);

            for (uint32_t group_index = 0u;
                 group_index < (uint32_t)dispatch_stage->binding_groups.size();
                 ++group_index)
            {
                StageBindingGroupStatic& descriptor = stage_static->binding_groups[group_index];
                vulkan.DestroyDynamicDescriptorPool(descriptor.pool);
            }

            registry.ComponentErase(node, stage_static);
        }
    });
}

void
DispatchContext::ClearFramebufferRuntime()
{
    graph.DepthFirstTraversal([this](uint32_t, dstk::Registry::Node node) {
        auto const* dispatch_pass = registry.ComponentLookup<layout::DispatchPass>(node);
        auto const* dispatch_stage = registry.ComponentLookup<layout::DispatchStage>(node);

        if (dispatch_pass
            && dispatch_pass->mode == layout::DispatchMode_Graphics)
        {
            auto* render_pass = registry.ComponentLookup<VkRenderPass>(node);
            if (render_pass)
            {
                vkDestroyRenderPass(vulkan.device, *render_pass, nullptr);
                registry.ComponentErase(node, render_pass);
            }

            auto* framebuffers = registry.ComponentLookup<std::vector<VkFramebuffer>>(node);
            if (framebuffers)
            {
                for (VkFramebuffer framebuffer : *framebuffers)
                    vkDestroyFramebuffer(vulkan.device, framebuffer, nullptr);
                registry.ComponentErase(node, framebuffers);
            }
        }

        if (dispatch_stage)
        {
            auto* pipeline = registry.ComponentLookup<VkPipeline>(node);
            if (pipeline)
            {
                vkDestroyPipeline(vulkan.device, *pipeline, nullptr);
                registry.ComponentErase(node, pipeline);
            }
        }
    });
}

void
DispatchContext::ClearDynamicRuntime()
{
    graph.DepthFirstTraversal([this](uint32_t, dstk::Registry::Node node) {
        auto const* dispatch_stage = registry.ComponentLookup<layout::DispatchStage>(node);
        auto* stage_static = registry.ComponentLookup<DispatchStageStatic>(node);
        auto* stage_dynamic = registry.ComponentLookup<DispatchStageDynamic>(node);

        if (dispatch_stage
            && stage_static
            && stage_dynamic)
        {
            for (uint32_t group_index = 0u; group_index < (uint32_t)dispatch_stage->binding_groups.size(); ++group_index)
            {
                StageBindingGroupDynamic& group_dynamic = stage_dynamic->binding_groups[group_index];
                StageBindingGroupStatic& group_static = stage_static->binding_groups[group_index];

                for (VkDescriptorSet set : group_dynamic.sets)
                    group_static.pool.FreeDescriptorSet(set);

                group_dynamic.sets.clear();
            }

            registry.ComponentErase(node, stage_dynamic);
        }
    });
}

void
DispatchContext::GenerateStaticRuntime()
{
    graph.DepthFirstTraversal([this](uint32_t, dstk::Registry::Node node) {
        auto const* dispatch_pass = registry.ComponentLookup<layout::DispatchPass>(node);
        auto const* dispatch_graph = registry.ComponentLookup<layout::DispatchGraph>(node);
        auto const* dispatch_stage = registry.ComponentLookup<layout::DispatchStage>(node);
        auto const* node_tag = registry.ComponentLookup<layout::NodeTag>(node);

        layout::DispatchGraph::Data* graph_data =
            *registry.ComponentLookup<layout::DispatchGraph::Data*>(node);
        std::string const& node_name = graph_data->name_lookup[node];

        if (dispatch_stage)
        {
            DispatchStageStatic stage_static = {};
            std::vector<VkDescriptorSetLayout> set_layouts = {};
            std::vector<VkPushConstantRange> push_constant_ranges = {};

            for (uint32_t group_index = 0u; group_index < (uint32_t)dispatch_stage->binding_groups.size(); ++group_index)
            {
                layout::StageBindingGroup const& binding_group = dispatch_stage->binding_groups[group_index];

                StageBindingGroupStatic descriptor = {};
                std::vector<vktk::Context::DescriptorSetBindingPoint> bindings = {};

                for (layout::StageBinding const& binding : binding_group.bindings)
                {
                    bindings.push_back(vktk::Context::DescriptorSetBindingPoint{
                        binding.type,
                        binding.flags,
                        binding.stages,
                        binding.count
                    });
                }

                descriptor.pool = vulkan.CreateDynamicDescriptorPool(bindings);
                stage_static.binding_groups.push_back(descriptor);
                set_layouts.push_back(descriptor.pool.layout);
            }

            for (layout::PushConstantRange const& push_constant : dispatch_stage->push_constants)
            {
                push_constant_ranges.push_back(VkPushConstantRange{
                    push_constant.stages,
                    push_constant.offset,
                    push_constant.size
                });
            }

            VkPipelineLayoutCreateInfo pipeline_create = {};
            pipeline_create.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            pipeline_create.pNext = nullptr;
            pipeline_create.flags = 0u;
            pipeline_create.setLayoutCount = set_layouts.size();
            pipeline_create.pSetLayouts = set_layouts.data();
            pipeline_create.pushConstantRangeCount = push_constant_ranges.size();
            pipeline_create.pPushConstantRanges = push_constant_ranges.data();
            CHECKCALL(vkCreatePipelineLayout, vulkan.device, &pipeline_create, nullptr,
                      &stage_static.pipeline_layout);

            registry.BindComponent(node, stage_static);
        }
    });
}

void
DispatchContext::GenerateFramebufferRuntime(VkFormat const* _swapchain_format)
{
    graph.DepthFirstTraversal([this, _swapchain_format](uint32_t,
                                                        dstk::Registry::Node node) {
        auto const* dispatch_pass = registry.ComponentLookup<layout::DispatchPass>(node);
        auto const* dispatch_stage = registry.ComponentLookup<layout::DispatchStage>(node);

        if (dispatch_pass
            && dispatch_pass->mode == layout::DispatchMode_Graphics)
            registry.BindComponent(
                node,
                vulkan.CreateRenderPass(RenderPassDescription(node, _swapchain_format)));

        if (dispatch_stage)
        {
            auto const& runtime = *registry.ComponentLookup<DispatchStageStatic>(node);

            dstk::Registry::Node dispatch_pass_node = dstk::FreeList::kInvalidIndex;
            layout::DispatchPass const* parent_pass = nullptr;
            {
                layout::DispatchGraph::Data* current_data =
                    *registry.ComponentLookup<layout::DispatchGraph::Data*>(node);
                while (current_data->parent_node)
                {
                    parent_pass =
                        registry.ComponentLookup<layout::DispatchPass>(current_data->parent_node);
                    if (parent_pass)
                    {
                        dispatch_pass_node = current_data->parent_node;
                        break;
                    }

                    current_data =
                        *registry.ComponentLookup<layout::DispatchGraph::Data*>(
                            current_data->parent_node);
                }
            }
            assert(parent_pass);

            auto const* render_pass = registry.ComponentLookup<VkRenderPass>(dispatch_pass_node);

            std::vector<vktk::Context::PipelineStage> pipeline_stages = {};
            for (layout::ShaderStage const& shader : dispatch_stage->shaders)
            {
                std::string shader_name = dispatch_stage->name;
                switch (shader.stage)
                {
                case VK_SHADER_STAGE_VERTEX_BIT: shader_name += "_vertex"; break;
                case VK_SHADER_STAGE_FRAGMENT_BIT: shader_name += "_fragment"; break;
                default: shader_name += "_unknown"; break;
                }

                VkShaderModule shader_module =
                    vulkan.CompileShader_SPIRV(shader.binary);

                pipeline_stages.push_back(vktk::Context::PipelineStage{ shader_module, shader.stage });
            }

            if (parent_pass->mode == layout::DispatchMode_Graphics)
            {
                std::vector<vktk::Context::BlendState> const* blend_states =
                    &dispatch_stage->graphics.blend_states;

                std::vector<vktk::Context::BlendState> default_blend_states = {};
                if (dispatch_stage->graphics.blend_states.empty())
                {
                    default_blend_states.resize(parent_pass->graphics.attachments.size());
                    for (vktk::Context::BlendState state : default_blend_states)
                        state.blend_enable = VK_FALSE;
                    blend_states = &default_blend_states;
                }

                VkPipeline pipeline = vulkan.CreatePipeline(
                    runtime.pipeline_layout,
                    *render_pass,
                    pipeline_stages,
                    dispatch_stage->graphics.state_overrides,
                    *blend_states,
                    dispatch_stage->graphics.topology,
                    dispatch_stage->graphics.input_layout
                    );

                registry.BindComponent(node, pipeline);
            }

            for (vktk::Context::PipelineStage const& pipeline_stage : pipeline_stages)
                vkDestroyShaderModule(vulkan.device, pipeline_stage.module, nullptr);
        }
    });
}

void
DispatchContext::ResetDynamicRuntime(bind::PNextMap const& _pNext_map)
{
    ClearDynamicRuntime();

    graph.DepthFirstTraversal([this, _pNext_map](uint32_t stack_depth,
                                                 dstk::Registry::Node node) {
        layout::DispatchGraph::Data* graph_data =
            *registry.ComponentLookup<layout::DispatchGraph::Data*>(node);
        std::string const& node_name = graph_data->name_lookup[node];

        auto const* dispatch_stage = registry.ComponentLookup<layout::DispatchStage>(node);

        if (dispatch_stage)
        {
            if (dispatch_stage->invocation_count)
            {
                layout::DispatchGraph::Data* graph_data =
                    *registry.ComponentLookup<layout::DispatchGraph::Data*>(node);
                graph_data->invoc_count[node] = dispatch_stage->invocation_count;
            }

            auto* stage_static = registry.ComponentLookup<DispatchStageStatic>(node);
            DispatchStageDynamic stage_dynamic = {};
            stage_dynamic.invocation_count = dispatch_stage->invocation_count;
            stage_dynamic.invoc_count_stack.resize(stack_depth);
            stage_dynamic.invoc_count_stack[0] = dispatch_stage->invocation_count;

            std::vector<bind::PNextEntry> stage_pnexts = {};
            if (_pNext_map.count(node_name))
                stage_pnexts = _pNext_map.at(node_name);

            for (uint32_t group_index = 0u; group_index < (uint32_t)dispatch_stage->binding_groups.size(); ++group_index)
            {
                StageBindingGroupStatic& binding_group_static = stage_static->binding_groups[group_index];
                StageBindingGroupDynamic binding_group_dynamic = {};
                binding_group_dynamic.sets.resize(stage_dynamic.invocation_count);

                void* pnext_begin = nullptr;

                VkDescriptorSetVariableDescriptorCountAllocateInfo desc_count_alloc;
                {
                    auto group_pnext_it = std::find_if(
                        stage_pnexts.begin(), stage_pnexts.end(),
                        [group_index](bind::PNextEntry const& entry) {
                            return entry.binding_group_index == group_index;
                        });

                    if (group_pnext_it != stage_pnexts.end())
                    {
                        void* desc_count_alloc_ptr = vktk::PNextLookup(
                            (VkBaseOutStructure*)group_pnext_it->pnext,
                            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO);
                        desc_count_alloc = desc_count_alloc_ptr
                            ? *(VkDescriptorSetVariableDescriptorCountAllocateInfo const*)desc_count_alloc_ptr
                            : VkDescriptorSetVariableDescriptorCountAllocateInfo{};
                        desc_count_alloc.pNext = nullptr;
                        pnext_begin = desc_count_alloc_ptr ? &desc_count_alloc : nullptr;
                    }
                }

                for (uint32_t index = 0; index < stage_dynamic.invocation_count; ++index)
                    binding_group_dynamic.sets[index] = binding_group_static.pool.AllocateDescriptorSet(pnext_begin);

                stage_dynamic.binding_groups.push_back(binding_group_dynamic);
            }

            registry.BindComponent(node, stage_dynamic);
        }
    });
}

void
DispatchContext::Dispatch(vktk::CommandBuffer& _command_buffer) const
{
    graph.DepthFirstTraversal(
        [this, &_command_buffer](uint32_t,
                                 dstk::Registry::Node node) {
        auto const* dispatch_pass = registry.ComponentLookup<layout::DispatchPass>(node);

        if (dispatch_pass) {
            auto const* area_binding = registry.ComponentLookup<bind::FramebufferArea>(node);
            layout::GraphicsNode const& graphics_info = dispatch_pass->graphics;

            std::vector<VkClearValue> clear_values = {};
            for (layout::RenderAttachment const& attachment : graphics_info.attachments) {
                VkClearValue clear_value = {};
                std::memcpy(&clear_value, &attachment.clear_value, sizeof(VkClearValue));
                clear_values.push_back(clear_value);
            }

            VkRect2D scissor{ VkOffset2D{ (int32_t)area_binding->scissor.min.x(),
                                          (int32_t)area_binding->scissor.min.y() },
                              VkExtent2D{ area_binding->scissor.extent.x(),
                                          area_binding->scissor.extent.y() } };

            VkViewport viewport{ area_binding->viewport.min.x(),
                                 area_binding->viewport.min.y(),
                                 area_binding->viewport.extent.x(),
                                 area_binding->viewport.extent.y(),
                                 0.f,
                                 1.f };

            vkCmdSetViewport(_command_buffer, 0, 1, &viewport);
            vkCmdSetScissor(_command_buffer, 0, 1, &scissor);

            auto const* dispatch_graph = registry.ComponentLookup<layout::DispatchGraph>(node);

            dispatch_graph->DepthFirstTraversal([this, &_command_buffer]
                                                (uint32_t, dstk::Registry::Node node) {
                auto const* dispatch_stage = registry.ComponentLookup<layout::DispatchStage>(node);
                auto const* stage_barriers = registry.ComponentLookup<bind::StageBarriers>(node);
                if (!dispatch_stage || !stage_barriers)
                    return;

                for (auto&& barrier : stage_barriers->buffer_barriers)
                    if (barrier.second.first.stage)
                        _command_buffer.BufferTransitions(barrier.second.first.stage,
                                                          barrier.second.second.stage,
                                                          { { barrier.first,
                                                              barrier.second.first.access,
                                                              barrier.second.second.access } });
            });

            VkRenderPass render_pass = *registry.ComponentLookup<VkRenderPass>(node);
            std::vector<VkFramebuffer> const& framebuffers = *registry.ComponentLookup<std::vector<VkFramebuffer>>(node);

            VkRenderPassBeginInfo rp_begin_info{};
            rp_begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            rp_begin_info.pNext = nullptr;
            rp_begin_info.renderPass = render_pass;
            rp_begin_info.framebuffer = framebuffers[area_binding->framebuffer_index];
            rp_begin_info.renderArea = scissor;
            rp_begin_info.clearValueCount = graphics_info.attachments.size();
            rp_begin_info.clearValueCount = clear_values.size();
            rp_begin_info.pClearValues = clear_values.data();
            vkCmdBeginRenderPass(_command_buffer, &rp_begin_info, VK_SUBPASS_CONTENTS_INLINE);

            dispatch_graph->DepthFirstInvocation([this, &_command_buffer, dispatch_pass]
                                                 (uint32_t,
                                                  vkrg::layout::DispatchGraph::InvocStack const& invoc_stack,
                                                  dstk::Registry::Node node) {
                auto const* dispatch_stage = registry.ComponentLookup<layout::DispatchStage>(node);
                auto const* pipeline = registry.ComponentLookup<VkPipeline>(node);

                if (!dispatch_stage || !pipeline)
                    return;

                layout::GraphicsNode const& graphics_info = dispatch_pass->graphics;
                auto const* stage_static = registry.ComponentLookup<DispatchStageStatic>(node);
                auto const* stage_dynamic = registry.ComponentLookup<DispatchStageDynamic>(node);

                vkCmdBindPipeline(_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, *pipeline);

                vkrg::layout::DispatchGraph::InvocStack invoc_indices = invoc_stack;
                invoc_indices.push_back(0);

                uint32_t invoc_count = std::max(stage_dynamic->invoc_count_stack.front(), 1u);
                for (uint32_t invoc_index = 0u;
                     invoc_index < invoc_count;
                     ++invoc_index)
                {
                    invoc_indices.front() = invoc_index;
                    uint32_t binding_index = ComputeBindingIndex(
                        stage_dynamic->invoc_count_stack,
                        invoc_indices
                    );

                    auto const* graphics_command = registry.ComponentLookup<bind::GraphicsCommand>(node, binding_index);
                    if (!graphics_command)
                        continue;

                    for (uint32_t group_index = 0u;
                         group_index < (uint32_t)dispatch_stage->binding_groups.size();
                         ++group_index)
                    {
                        VkDescriptorSet descset =
                            stage_dynamic->binding_groups[group_index].sets[binding_index];
                        vkCmdBindDescriptorSets(_command_buffer,
                                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                stage_static->pipeline_layout,
                                                group_index,
                                                1u, &descset,
                                                0u, nullptr);
                    }

                    for (uint32_t range_index = 0u;
                         range_index < (uint32_t)dispatch_stage->push_constants.size();
                         ++range_index)
                    {
                        layout::PushConstantRange const& range = dispatch_stage->push_constants[range_index];
                        void* memory = registry.ComponentRequest<bind::PushConstantStorage>(node, binding_index)[range_index].get();
                        vkCmdPushConstants(_command_buffer,
                                           stage_static->pipeline_layout,
                                           range.stages,
                                           range.offset,
                                           range.size,
                                           memory);
                    }

                    auto const* vertices = registry.ComponentLookup<bind::VertexBindings>(
                        node, binding_index);
                    bool indexed_draw = false;
                    if (vertices)
                    {
                        for (uint32_t vertex_input_index = 0u;
                             vertex_input_index < (uint32_t)vertices->vertex_inputs.size();
                             ++vertex_input_index)
                        {
                            bind::VertexInput const& binding = vertices->vertex_inputs[vertex_input_index];
                            if (binding.buffer == VK_NULL_HANDLE)
                                continue;

                            vkCmdBindVertexBuffers(_command_buffer, vertex_input_index, 1,
                                                   &binding.buffer, &binding.offset);
                        }

                        indexed_draw = (vertices->index_input.buffer != VK_NULL_HANDLE);
                        if (indexed_draw)
                            vkCmdBindIndexBuffer(_command_buffer,
                                                 vertices->index_input.buffer,
                                                 vertices->index_input.offset,
                                                 VK_INDEX_TYPE_UINT32);
                    }

                    switch (graphics_command->command)
                    {
                    case bind::DispatchCommand_Draw:
                    {
                        if (!indexed_draw)
                            vkCmdDraw(_command_buffer,
                                      graphics_command->vertex_count,
                                      graphics_command->instance_count,
                                      0, 0);
                        else
                            vkCmdDrawIndexed(_command_buffer,
                                             graphics_command->vertex_count,
                                             graphics_command->instance_count,
                                             0, 0, 0);
                    } break;
                    default: break;
                    }

                    registry.ComponentRangeErase<bind::ShaderBinding>(node);
                }
            });

            vkCmdEndRenderPass(_command_buffer);
        }

        layout::DispatchGraph::Data const* graph_data =
            *registry.ComponentLookup<layout::DispatchGraph::Data*>(node);
#if 0
        if (graph_data->edges_in.count(node))
        {
            for (dstk::Registry::Node edge : graph_data->edges_in.at(node))
            {
                layout::DispatchGraph::EdgeDesc const& edge_desc =
                    *registry.ComponentLookup<layout::DispatchGraph::EdgeDesc>(edge);
                layout::EdgeBindings const* bindings =
                    registry.ComponentLookup<layout::EdgeBindings>(edge);

                if (!bindings
                    || bindings->source.type == layout::BindingType_Framebuffer
                    || bindings->sink.type == layout::BindingType_Framebuffer
                    || bindings->type != layout::EdgeType_Image)
                    continue;

                layout::BindingPointSync source_reqs =
                    BindingPointSyncRequirements(edge_desc.source,
                                                 bindings->type,
                                                 bindings->source);
                layout::BindingPointSync sink_reqs =
                    BindingPointSyncRequirements(node,
                                                 bindings->type,
                                                 bindings->sink);

                VkImage image = {};
                if (bindings->sink.type == layout::BindingType_BlitImage)
                {
                    bind::BlitBindings const& blit_bindings =
                        *registry.ComponentLookup<bind::BlitBindings>(node);

                    if (bindings->sink.blit_slot == layout::BlitImageSlot_Source)
                        image = blit_bindings.source;
                    if (bindings->sink.blit_slot == layout::BlitImageSlot_Sink)
                        image = blit_bindings.sink;
                }

                _command_buffer.ImageTransitions(
                    source_reqs.stage, sink_reqs.stage,
                    {
                        {
                            image,
                            source_reqs.access, sink_reqs.access,
                            source_reqs.layout, sink_reqs.layout,
                            VK_IMAGE_ASPECT_COLOR_BIT
                        }
                    }
                );
            }
        }
#endif

        layout::NodeTag const* tag = registry.ComponentLookup<layout::NodeTag>(node);
        if (tag && *tag == layout::NodeTag_BlitImage)
        {
            auto const* blit_bindings = registry.ComponentLookup<bind::BlitBindings>(node);
            numtk::vec3i src_min = blit_bindings->command.source_bounds.min.cast<int32_t>();
            numtk::vec3i src_max = blit_bindings->command.source_bounds.max.cast<int32_t>();
            numtk::vec3i dst_min = blit_bindings->command.sink_bounds.min.cast<int32_t>();
            numtk::vec3i dst_max = blit_bindings->command.sink_bounds.max.cast<int32_t>();

            VkImageBlit blit_region = {};
            blit_region.srcSubresource = VkImageSubresourceLayers{
                blit_bindings->command.aspect_mask, 0, 0, VK_REMAINING_ARRAY_LAYERS
            };
            blit_region.srcOffsets[0] = { src_min.x, src_min.y, src_min.z };
            blit_region.srcOffsets[1] = { src_max.x, src_max.y, src_max.z };
            blit_region.dstSubresource = blit_region.srcSubresource;
            blit_region.dstOffsets[0] = { dst_min.x, dst_min.y, dst_min.z };
            blit_region.dstOffsets[1] = { dst_max.x, dst_max.y, dst_max.z };

            vkCmdBlitImage(_command_buffer,
                           blit_bindings->source,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           blit_bindings->sink,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1,
                           &blit_region,
                           blit_bindings->command.filter);
        }

        if (graph_data->edges_out.count(node))
        {
            for (dstk::Registry::Node edge : graph_data->edges_out.at(node))
            {
                layout::DispatchGraph::EdgeDesc const& edge_desc =
                    *registry.ComponentLookup<layout::DispatchGraph::EdgeDesc>(edge);
                layout::EdgeBindings const* bindings =
                    registry.ComponentLookup<layout::EdgeBindings>(edge);

                if (!bindings
                    || bindings->source.type == layout::BindingType_Framebuffer
                    || bindings->sink.type == layout::BindingType_Framebuffer
                    || bindings->type != layout::EdgeType_Image)
                    continue;

                dstk::Registry::Node source_node =
                    (bindings->source.node_override == dstk::FreeList::kInvalidIndex)
                    ? edge_desc.source
                    : bindings->source.node_override;

                dstk::Registry::Node sink_node =
                    (bindings->sink.node_override == dstk::FreeList::kInvalidIndex)
                    ? edge_desc.sink
                    : bindings->sink.node_override;

                layout::BindingPointSync source_reqs =
                    BindingPointSyncRequirements(source_node,
                                                 bindings->type,
                                                 bindings->source,
                                                 layout::EdgeBindingSlot_Source);
                layout::BindingPointSync sink_reqs =
                    BindingPointSyncRequirements(sink_node,
                                                 bindings->type,
                                                 bindings->sink,
                                                 layout::EdgeBindingSlot_Sink);

                VkImage image = {};
                if (bindings->source.type == layout::BindingType_BlitImage)
                {
                    bind::BlitBindings const& blit_bindings =
                        *registry.ComponentLookup<bind::BlitBindings>(source_node);

                    if (bindings->source.blit_slot == layout::BlitImageSlot_Source)
                        image = blit_bindings.source;
                    if (bindings->source.blit_slot == layout::BlitImageSlot_Sink)
                        image = blit_bindings.sink;
                }

                if (bindings->sink.type == layout::BindingType_BlitImage)
                {
                    bind::BlitBindings const& blit_bindings =
                        *registry.ComponentLookup<bind::BlitBindings>(sink_node);

                    if (bindings->sink.blit_slot == layout::BlitImageSlot_Source)
                        image = blit_bindings.source;
                    if (bindings->sink.blit_slot == layout::BlitImageSlot_Sink)
                        image = blit_bindings.sink;
                }

                _command_buffer.ImageTransitions(
                    source_reqs.stage, sink_reqs.stage,
                    {
                        {
                            image,
                            source_reqs.access, sink_reqs.access,
                            source_reqs.layout, sink_reqs.layout,
                            VK_IMAGE_ASPECT_COLOR_BIT
                        }
                    }
                );
            }
        }
    });
}

void
DispatchContext::ScopeOpenDescriptorPools()
{
    std::vector<layout::DispatchStage const*> stages =
        registry.ComponentGlobalLookup<layout::DispatchStage>();

    for (layout::DispatchStage const* stage : stages)
    {
        dstk::Registry::Node node = registry.NodeLookup(stage);
        std::vector<vktk::DynamicDescriptorPool>& pools =
            registry.ComponentRequest<std::vector<vktk::DynamicDescriptorPool>>(node);

        for (layout::StageBindingGroup const& binding_group : stage->binding_groups)
        {
            std::vector<vktk::Context::DescriptorSetBindingPoint> bindings = {};
            for (layout::StageBinding const& binding : binding_group.bindings)
                bindings.push_back(vktk::Context::DescriptorSetBindingPoint{
                    binding.type,
                    binding.flags,
                    binding.stages,
                    binding.count
                });

            pools.push_back(vulkan.CreateDynamicDescriptorPool(bindings));
        }
    }
}

void
DispatchContext::ScopeCloseDescriptorPools()
{
    std::vector<layout::DispatchStage const*> stages =
        registry.ComponentGlobalLookup<layout::DispatchStage>();

    for (layout::DispatchStage const* stage : stages)
    {
        dstk::Registry::Node node = registry.NodeLookup(stage);
        std::vector<vktk::DynamicDescriptorPool>& pools =
            *registry.ComponentLookup<std::vector<vktk::DynamicDescriptorPool>>(node);

        for (vktk::DynamicDescriptorPool& pool : pools)
            vulkan.DestroyDynamicDescriptorPool(pool);

        registry.ComponentErase(node, &pools);
    }
}

void
DispatchContext::ScopeOpenPipelineLayouts()
{
    std::vector<layout::DispatchStage const*> stages =
        registry.ComponentGlobalLookup<layout::DispatchStage>();

    for (layout::DispatchStage const* stage : stages)
    {
        dstk::Registry::Node node = registry.NodeLookup(stage);

        std::vector<vktk::DynamicDescriptorPool> const& pools =
            *registry.ComponentLookup<std::vector<vktk::DynamicDescriptorPool>>(node);
        std::vector<VkDescriptorSetLayout> set_layouts = {};
        for (vktk::DynamicDescriptorPool const& pool : pools)
            set_layouts.push_back(pool.layout);

        std::vector<VkPushConstantRange> push_constant_ranges = {};
        for (layout::PushConstantRange const& push_constants : stage->push_constants)
            push_constant_ranges.push_back(VkPushConstantRange{
                push_constants.stages,
                push_constants.offset,
                push_constants.size
            });

        VkPipelineLayout pipeline_layout = {};
        VkPipelineLayoutCreateInfo pipeline_create = {};
        pipeline_create.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipeline_create.pNext = nullptr;
        pipeline_create.flags = 0u;
        pipeline_create.setLayoutCount = set_layouts.size();
        pipeline_create.pSetLayouts = set_layouts.data();
        pipeline_create.pushConstantRangeCount = push_constant_ranges.size();
        pipeline_create.pPushConstantRanges = push_constant_ranges.data();
        CHECKCALL(vkCreatePipelineLayout, vulkan.device, &pipeline_create, nullptr,
                  &pipeline_layout);

        registry.BindComponent(node, pipeline_layout);
    }
}

void
DispatchContext::ScopeClosePipelineLayouts()
{
    std::vector<layout::DispatchStage const*> stages =
        registry.ComponentGlobalLookup<layout::DispatchStage>();

    for (layout::DispatchStage const* stage : stages)
    {
        dstk::Registry::Node node = registry.NodeLookup(stage);
        VkPipelineLayout* pipeline_layout = registry.ComponentLookup<VkPipelineLayout>(node);
        vkDestroyPipelineLayout(vulkan.device, *pipeline_layout, nullptr);
        registry.ComponentErase(node, pipeline_layout);
    }
}

void
DispatchContext::ScopeOpenRenderPasses(VkFormat _dynamic_format)
{
    std::vector<layout::DispatchPass const*> passes =
        registry.ComponentGlobalLookup<layout::DispatchPass>();

    for (layout::DispatchPass const* pass : passes)
    {
        if (pass->mode != layout::DispatchMode_Graphics)
            continue;

        dstk::Registry::Node node = registry.NodeLookup(pass);
        registry.BindComponent(
            node,
            vulkan.CreateRenderPass(
                RenderPassDescription(node, _dynamic_format)));
    }
}

void
DispatchContext::ScopeCloseRenderPasses()
{
    std::vector<layout::DispatchPass const*> passes =
        registry.ComponentGlobalLookup<layout::DispatchPass>();

    for (layout::DispatchPass const* pass : passes)
    {
        if (pass->mode != layout::DispatchMode_Graphics)
            continue;

        dstk::Registry::Node node = registry.NodeLookup(pass);
        VkRenderPass* render_pass = registry.ComponentLookup<VkRenderPass>(node);
        vkDestroyRenderPass(vulkan.device, *render_pass, nullptr);
        registry.ComponentErase(node, render_pass);
    }
}

void
DispatchContext::ScopeOpenPipelines()
{
    std::vector<layout::DispatchStage const*> stages =
        registry.ComponentGlobalLookup<layout::DispatchStage>();

    for (layout::DispatchStage const* stage : stages)
    {
        dstk::Registry::Node node = registry.NodeLookup(stage);

        dstk::Registry::Node parent_pass_node = dstk::FreeList::kInvalidIndex;
        layout::DispatchPass const* parent_pass = nullptr;
        {
            layout::DispatchGraph::Data* current_data =
                *registry.ComponentLookup<layout::DispatchGraph::Data*>(node);
            while (current_data->parent_node)
            {
                parent_pass =
                    registry.ComponentLookup<layout::DispatchPass>(current_data->parent_node);
                if (parent_pass)
                {
                    parent_pass_node = current_data->parent_node;
                    break;
                }

                current_data =
                    *registry.ComponentLookup<layout::DispatchGraph::Data*>(
                        current_data->parent_node);
            }
        }
        assert(parent_pass);

        VkRenderPass render_pass = *registry.ComponentLookup<VkRenderPass>(parent_pass_node);
        VkPipelineLayout pipeline_layout = *registry.ComponentLookup<VkPipelineLayout>(node);

        std::vector<vktk::Context::PipelineStage> pipeline_stages = {};
        for (layout::ShaderStage const& shader : stage->shaders)
        {
            std::string shader_name = stage->name;
            switch (shader.stage)
            {
            case VK_SHADER_STAGE_VERTEX_BIT: shader_name += "_vertex"; break;
            case VK_SHADER_STAGE_FRAGMENT_BIT: shader_name += "_fragment"; break;
            default: shader_name += "_unknown"; break;
            }

            VkShaderModule shader_module = vulkan.CompileShader_SPIRV(shader.binary);
            pipeline_stages.push_back(vktk::Context::PipelineStage{
                shader_module,
                shader.stage
            });
        }

        if (parent_pass->mode == layout::DispatchMode_Graphics)
        {
            std::vector<vktk::Context::BlendState> const* blend_states =
                &stage->graphics.blend_states;

            std::vector<vktk::Context::BlendState> default_blend_states = {};
            if (stage->graphics.blend_states.empty())
            {
                default_blend_states.resize(parent_pass->graphics.attachments.size());
                for (vktk::Context::BlendState state : default_blend_states)
                    state.blend_enable = VK_FALSE;
                blend_states = &default_blend_states;
            }

            VkPipeline pipeline = vulkan.CreatePipeline(
                pipeline_layout,
                render_pass,
                pipeline_stages,
                stage->graphics.state_overrides,
                *blend_states,
                stage->graphics.topology,
                stage->graphics.input_layout
            );

            registry.BindComponent(node, pipeline);
        }

        for (vktk::Context::PipelineStage const& pipeline_stage : pipeline_stages)
            vkDestroyShaderModule(vulkan.device, pipeline_stage.module, nullptr);
    }
}

void
DispatchContext::ScopeClosePipelines()
{
    std::vector<layout::DispatchStage const*> stages =
        registry.ComponentGlobalLookup<layout::DispatchStage>();

    for (layout::DispatchStage const* stage : stages)
    {
        dstk::Registry::Node node = registry.NodeLookup(stage);

        VkPipeline* pipeline = registry.ComponentLookup<VkPipeline>(node);
        if (!pipeline)
            continue;

        vkDestroyPipeline(vulkan.device, *pipeline, nullptr);
        registry.ComponentErase(node, pipeline);
    }
}

std::vector<vktk::Context::RenderPassAttachment>
DispatchContext::RenderPassDescription(
    dstk::Registry::Node _node,
    VkFormat const* _swapchain_format) const
{
    auto const& dispatch_pass = *registry.ComponentLookup<layout::DispatchPass>(_node);

    std::vector<vktk::Context::RenderPassAttachment> output = {};

    for (uint32_t index = 0u; index < (uint32_t)dispatch_pass.graphics.attachments.size(); ++index)
    {
        layout::BindingPoint binding_point = {};
        binding_point.type = layout::BindingType_Framebuffer;
        binding_point.attachment_index = index;
        layout::BindingPointSync sync_reqs =
            BindingPointSyncRequirements(_node,
                                         layout::EdgeType_Image,
                                         binding_point,
                                         layout::EdgeBindingSlot_Source);

        output.push_back(vktk::Context::RenderPassAttachment{
            dispatch_pass.graphics.attachments[index].format,
            dispatch_pass.graphics.attachments[index].initial_layout,
            sync_reqs.layout,
            dispatch_pass.graphics.attachments[index].load_op
        });
    }

    for (dstk::Registry::Node edge : (
             graph.data->edges_out.count(_node)
             ? graph.data->edges_out.at(_node)
             : std::vector<dstk::Registry::Node>{}))
    {
        layout::DispatchGraph::EdgeDesc const& edge_desc =
            *registry.ComponentLookup<layout::DispatchGraph::EdgeDesc>(edge);

        layout::EdgeBindings const* bindings = registry.ComponentLookup<layout::EdgeBindings>(edge);
        if (!bindings
            || bindings->source.type != layout::BindingType_Framebuffer)
            continue;

        vktk::Context::RenderPassAttachment& desc = output[bindings->source.attachment_index];
        layout::BindingPointSync sync_reqs =
            BindingPointSyncRequirements(edge_desc.sink,
                                         bindings->type,
                                         bindings->sink,
                                         layout::EdgeBindingSlot_Sink);
        desc.finalLayout = sync_reqs.layout;

        if (_swapchain_format && sync_reqs.layout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
            desc.format = *_swapchain_format;
    }

    for (dstk::Registry::Node edge : (
             graph.data->edges_in.count(_node)
             ? graph.data->edges_in.at(_node)
             : std::vector<dstk::Registry::Node>{}))
    {
        layout::DispatchGraph::EdgeDesc const& edge_desc =
            *registry.ComponentLookup<layout::DispatchGraph::EdgeDesc>(edge);

        layout::EdgeBindings const* bindings = registry.ComponentLookup<layout::EdgeBindings>(edge);
        if (!bindings
            || bindings->sink.type != layout::BindingType_Framebuffer)
            continue;

        vktk::Context::RenderPassAttachment& desc = output[bindings->sink.attachment_index];
        layout::BindingPointSync sync_reqs =
            BindingPointSyncRequirements(edge_desc.source,
                                         bindings->type,
                                         bindings->source,
                                         layout::EdgeBindingSlot_Source);
        desc.initialLayout = sync_reqs.layout;
    }

    return output;
}

layout::BindingPointSync
DispatchContext::BindingPointSyncRequirements(
    dstk::Registry::Node _node,
    layout::EdgeType _type,
    layout::BindingPoint const& _binding_point,
    layout::EdgeBindingSlot _edge_slot) const
{
    if (_binding_point.node_override != dstk::FreeList::kInvalidIndex)
        _node = _binding_point.node_override;

    layout::BindingPointSync output = {};
    output.access = VK_ACCESS_NONE;
    output.layout = VK_IMAGE_LAYOUT_UNDEFINED;
    output.stage = (_edge_slot == layout::EdgeBindingSlot_Source)
        ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
        : VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

    if (_type == layout::EdgeType_Image)
    {
        switch (_binding_point.type)
        {

        case layout::BindingType_Shader:
        {
            layout::DispatchStage const& stage = *registry.ComponentLookup<layout::DispatchStage>(_node);
            layout::StageBinding const& binding = stage
                .binding_groups[_binding_point.shader.group_index]
                .bindings[_binding_point.shader.binding_index];

            switch (binding.type)
            {

            case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER: {
                output.access = VK_ACCESS_SHADER_READ_BIT;
                output.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            } break;

            case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE: {
                output.access = VK_ACCESS_SHADER_READ_BIT;
                output.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            } break;

            case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE: {
                output.access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
                output.layout = VK_IMAGE_LAYOUT_GENERAL;
            } break;

            default: break;
            }
        } break;

        case layout::BindingType_Framebuffer:
        {
            layout::DispatchPass const& pass = *registry.ComponentLookup<layout::DispatchPass>(_node);
            layout::RenderAttachment const& attachment_info =
                pass.graphics.attachments[_binding_point.attachment_index];

            if (attachment_info.format >= VK_FORMAT_D16_UNORM
                && attachment_info.format < VK_FORMAT_BC1_RGB_UNORM_BLOCK)
            {
                output.access = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT
                    | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                output.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            }
            else
            {
                output.access = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT
                    | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                output.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            }
        } break;

        default: {
            layout::NodeTag const* tag = registry.ComponentLookup<layout::NodeTag>(_node);
            if (!tag)
                break;

            if (*tag == layout::NodeTag_Present)
            {
                output.stage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
                output.access = 0;
                output.layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            }
            else if (*tag == layout::NodeTag_BlitImage)
            {
                output.stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
                output.access = VK_ACCESS_TRANSFER_READ_BIT;
                if (_binding_point.blit_slot == layout::BlitImageSlot_Source)
                    output.layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                else if (_binding_point.blit_slot == layout::BlitImageSlot_Sink)
                    output.layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            }
        } break;
        }
    }

    return output;
}

void
DispatchContext::UpdateDynamicInvocationCount(dstk::Registry::Node _node,
                                              uint32_t _invocation_count)
{
    static auto const update_descriptor_allocs = [](layout::DispatchStage const* stage_layout,
                                                    DispatchStageDynamic& stage_dynamic,
                                                    DispatchStageStatic& stage_static,
                                                    uint32_t invoc_count,
                                                    uint32_t stack_index) {
        std::vector<uint32_t> old_count_stack = stage_dynamic.invoc_count_stack;
        stage_dynamic.invoc_count_stack[stack_index] = invoc_count;
        std::vector<uint32_t> const& new_count_stack = stage_dynamic.invoc_count_stack;

        uint32_t old_max = 0u;
        uint32_t old_count = std::accumulate(
            old_count_stack.begin(), old_count_stack.end(), 1u,
            [&old_max](uint32_t lhs, uint32_t rhs) {
                old_max = std::max(old_max, rhs);
                return lhs * std::max(1u, rhs); }
        );
        if (!old_max)
            old_count = 0;

        uint32_t new_max = 0u;
        uint32_t new_count = std::accumulate(
            new_count_stack.begin(), new_count_stack.end(), 1u,
            [&new_max](uint32_t lhs, uint32_t rhs) {
                new_max = std::max(new_max, rhs);
                return lhs * std::max(1u, rhs); }
        );
        if (!new_max)
            new_count = 0;

        for (uint32_t group_index = 0u;
             group_index < (uint32_t)stage_layout->binding_groups.size();
             ++group_index)
        {
            StageBindingGroupDynamic& group_dynamic = stage_dynamic.binding_groups[group_index];
            StageBindingGroupStatic& group_static = stage_static.binding_groups[group_index];

            if (new_count > old_count)
            {
                group_dynamic.sets.reserve(new_count);
                for (uint32_t index = old_count; index < new_count; ++index)
                    group_dynamic.sets.push_back(group_static.pool.AllocateDescriptorSet());
            }
            else
            {
                for (uint32_t index = new_count; index < old_count; ++index)
                    group_static.pool.FreeDescriptorSet(group_dynamic.sets[index]);
                group_dynamic.sets.resize(new_count);
            }
        }
    };

    layout::DispatchGraph::Data* graph_data =
        *registry.ComponentLookup<layout::DispatchGraph::Data*>(_node);
    graph_data->invoc_count[_node] = _invocation_count;

    layout::DispatchStage const* stage_layout = registry.ComponentLookup<layout::DispatchStage>(_node);
    if (stage_layout)
    {
        DispatchStageDynamic& stage_dynamic = *registry.ComponentLookup<DispatchStageDynamic>(_node);
        if (stage_dynamic.invoc_count_stack[0] == _invocation_count)
            return;

        DispatchStageStatic& stage_static = *registry.ComponentLookup<DispatchStageStatic>(_node);
        update_descriptor_allocs(stage_layout, stage_dynamic, stage_static, _invocation_count, 0);
    }

    layout::DispatchGraph const* subgraph = registry.ComponentLookup<layout::DispatchGraph>(_node);
    if (subgraph)
    {
        subgraph->DepthFirstTraversal([this, _invocation_count, base_depth = subgraph->Depth()]
                                      (uint32_t stack_depth,
                                       dstk::Registry::Node node) {
            layout::DispatchStage const* stage_layout = registry.ComponentLookup<layout::DispatchStage>(node);
            if (!stage_layout)
                return;

            DispatchStageDynamic& stage_dynamic =
                *registry.ComponentLookup<DispatchStageDynamic>(node);
            DispatchStageStatic& stage_static =
                *registry.ComponentLookup<DispatchStageStatic>(node);

            update_descriptor_allocs(stage_layout,
                                     stage_dynamic,
                                     stage_static,
                                     _invocation_count,
                                     stack_depth-base_depth);
        });
    }
}

void
DispatchContext::BindBarrier(dstk::Registry::Node _node,
                             bind::ResourceBarrier const& _barrier)
{
    bind::StageBarriers& stage_barriers = registry.ComponentRequest<bind::StageBarriers>(_node);
    stage_barriers.barriers.push_back(_barrier);
    if (_barrier.type == bind::BufferResource)
        stage_barriers.buffer_barriers[_barrier.buffer].first = bind::AccessInfo{
            _barrier.stage,
            _barrier.access
        };
}

void
DispatchContext::ClearStageBarriers(dstk::Registry::Node _node)
{
    bind::StageBarriers& stage_barriers = registry.ComponentRequest<bind::StageBarriers>(_node);
    stage_barriers.barriers.clear();
    stage_barriers.buffer_barriers.clear();
}

void
DispatchContext::BindSwapchain(dstk::Registry::Node _node,
                               vktk::Swapchain const& _swapchain)
{
    VkRenderPass render_pass = *registry.ComponentLookup<VkRenderPass>(_node);
    std::vector<VkFramebuffer> framebuffers = {};
    for (uint32_t index = 0u; index < (uint32_t)_swapchain.framebuffers.size(); ++index)
    {
        framebuffers.push_back(
            vulkan.CreateFramebuffer(render_pass,
                                     { _swapchain.size.width, _swapchain.size.height },
                                     { _swapchain.views[index*2], _swapchain.views[index*2 + 1] })
            );
    }
    registry.ComponentRequest<std::vector<VkFramebuffer>>(_node) = framebuffers;
}

void
DispatchContext::BindAttachments(dstk::Registry::Node _node,
                                 numtk::vec2u const& _size,
                                 std::vector<VkImageView> const& _attachments)
{
    std::vector<VkFramebuffer>& framebuffers = registry.ComponentRequest<std::vector<VkFramebuffer>>(_node);
    VkRenderPass render_pass = *registry.ComponentLookup<VkRenderPass>(_node);
    framebuffers.push_back(vulkan.CreateFramebuffer(render_pass, _size, _attachments));
}

void
DispatchContext::BindFramebufferArea(dstk::Registry::Node _node,
                                     bind::FramebufferArea const& _area)
{
    registry.ComponentRequest<bind::FramebufferArea>(_node) = _area;
}

void
DispatchContext::BindBlitCommand(dstk::Registry::Node _node,
                                 bind::BlitCommand const& _command)
{
    registry.ComponentRequest<bind::BlitBindings>(_node).command = _command;
}

void
DispatchContext::BindImage(dstk::Registry::Node _node,
                           layout::BindingPoint const& _location,
                           VkImage _image)
{
    if (_location.type == layout::BindingType_BlitImage)
    {
        bind::BlitBindings& bindings = registry.ComponentRequest<bind::BlitBindings>(_node);
        if (_location.blit_slot == layout::BlitImageSlot_Source)
            bindings.source = _image;
        if (_location.blit_slot == layout::BlitImageSlot_Sink)
            bindings.sink = _image;
    }
}

uint32_t
DispatchContext::ComputeBindingIndex(std::vector<uint32_t> const& _invoc_count_stack,
                                     std::vector<uint32_t> const& _invoc_indices)
{
    uint32_t binding_index = _invoc_indices[0];
    for (uint32_t index = 1u; index < _invoc_indices.size(); ++index)
        binding_index += _invoc_indices[index] * _invoc_count_stack[index-1];
    return binding_index;
}

void
DispatchContext::BindGraphicsCommand(dstk::Registry::Node _node,
                                     std::vector<uint32_t> const& _invoc_indices,
                                     bind::GraphicsCommand const& _command)
{
    DispatchStageDynamic const& stage_dynamic =
        *registry.ComponentLookup<DispatchStageDynamic>(_node);
    uint32_t const binding_index = ComputeBindingIndex(stage_dynamic.invoc_count_stack,
                                                       _invoc_indices);

    registry.ComponentRequest<bind::GraphicsCommand>(_node, binding_index) = _command;
}

void
DispatchContext::BindGraphicsCommand(dstk::Registry::Node _node,
                                     uint32_t _invoc_index,
                                     bind::GraphicsCommand const& _command)
{
    BindGraphicsCommand(_node, std::vector<uint32_t>{ _invoc_index }, _command);
}

void
DispatchContext::BindIndexInput(dstk::Registry::Node _node,
                                std::vector<uint32_t> const& _invoc_indices,
                                bind::IndexInput const& _index_input)
{
    DispatchStageDynamic const& stage_dynamic =
        *registry.ComponentLookup<DispatchStageDynamic>(_node);
    uint32_t const binding_index = ComputeBindingIndex(stage_dynamic.invoc_count_stack,
                                                       _invoc_indices);

    bind::StageBarriers& stage_barriers = registry.ComponentRequest<bind::StageBarriers>(_node);
    bind::VertexBindings& vertices = registry.ComponentRequest<bind::VertexBindings>(_node, binding_index);
    vertices.index_input = _index_input;
    stage_barriers.buffer_barriers[_index_input.buffer].second = bind::AccessInfo{
        VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
        VK_ACCESS_INDEX_READ_BIT
    };
}

void
DispatchContext::BindIndexInput(dstk::Registry::Node _node,
                                uint32_t _invoc_index,
                                bind::IndexInput const& _index_input)
{
    BindIndexInput(_node, std::vector<uint32_t>{ _invoc_index }, _index_input);
}

void
DispatchContext::BindVertexInput(dstk::Registry::Node _node,
                                 layout::BindingPoint const& _location,
                                 std::vector<uint32_t> const& _invoc_indices,
                                 bind::VertexInput const& _vertex_input)
{
    DispatchStageDynamic const& stage_dynamic =
        *registry.ComponentLookup<DispatchStageDynamic>(_node);
    uint32_t const binding_index = ComputeBindingIndex(stage_dynamic.invoc_count_stack,
                                                       _invoc_indices);

    bind::StageBarriers& stage_barriers = registry.ComponentRequest<bind::StageBarriers>(_node);
    bind::VertexBindings& vertices = registry.ComponentRequest<bind::VertexBindings>(_node, binding_index);

    if (vertices.vertex_inputs.size() < _location.vertex_input_index+1)
        vertices.vertex_inputs.resize(_location.vertex_input_index+1);
    vertices.vertex_inputs[_location.vertex_input_index] = _vertex_input;

    stage_barriers.buffer_barriers[_vertex_input.buffer].second = bind::AccessInfo{
        VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
        VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT
    };
}

void
DispatchContext::BindVertexInput(dstk::Registry::Node _node,
                                 layout::BindingPoint const& _location,
                                 uint32_t _invoc_index,
                                 bind::VertexInput const& _vertex_input)
{
    BindVertexInput(_node, _location, std::vector<uint32_t>{ _invoc_index }, _vertex_input);
}

void*
DispatchContext::BindPushConstants(dstk::Registry::Node _node,
                                   layout::BindingPoint const& _location,
                                   std::vector<uint32_t> const& _invoc_indices)
{
    DispatchStageDynamic const& stage_dynamic =
        *registry.ComponentLookup<DispatchStageDynamic>(_node);
    uint32_t const binding_index = ComputeBindingIndex(stage_dynamic.invoc_count_stack,
                                                       _invoc_indices);

    layout::DispatchStage const& stage = *registry.ComponentLookup<layout::DispatchStage>(_node);
    layout::PushConstantRange const& range = stage.push_constants[_location.push_constant_range];
    bind::PushConstantStorage& storage = registry.ComponentRequest<bind::PushConstantStorage>(_node, binding_index);
    if (storage.size() < _location.push_constant_range+1)
        storage.resize(_location.push_constant_range+1);
    storage[_location.push_constant_range].reset(new uint8_t[range.size]);
    return storage[_location.push_constant_range].get();
}

void*
DispatchContext::BindPushConstants(dstk::Registry::Node _node,
                                   layout::BindingPoint const& _location,
                                   uint32_t _invoc_index)
{
    return BindPushConstants(_node, _location, std::vector<uint32_t>{ _invoc_index });
}

VkWriteDescriptorSet
DispatchContext::BindShaderResource(dstk::Registry::Node _node,
                                    layout::BindingPoint const& _location,
                                    std::vector<uint32_t> const& _invoc_indices,
                                    bind::ShaderResources const& _resources)
{
    DispatchStageDynamic const& stage_dynamic = *registry.ComponentLookup<DispatchStageDynamic>(_node);
    uint32_t const binding_index = ComputeBindingIndex(stage_dynamic.invoc_count_stack,
                                                       _invoc_indices);

    StageBindingGroupDynamic const& group_runtime = stage_dynamic.binding_groups[_location.shader.group_index];

    layout::DispatchStage const& stage = *registry.ComponentLookup<layout::DispatchStage>(_node);
    layout::StageBindingGroup const& binding_group = stage.binding_groups[_location.shader.group_index];
    layout::StageBinding const& binding = binding_group.bindings[_location.shader.binding_index];

    bind::ShaderBinding& shader_binding =
        *registry.BindComponent(_node, binding_index, bind::ShaderBinding{ _location, _resources });

    uint32_t descriptor_count = 0u;
    if (!shader_binding.resources.images.empty())
        descriptor_count = shader_binding.resources.images.size();
    if (!shader_binding.resources.buffers.empty())
        descriptor_count = shader_binding.resources.buffers.size();

    VkWriteDescriptorSet desc_write = {};
    desc_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    desc_write.pNext = nullptr;
    desc_write.dstSet = group_runtime.sets[binding_index];
    desc_write.dstBinding = shader_binding.target.shader.binding_index;
    desc_write.dstArrayElement = 0u;
    desc_write.descriptorCount = descriptor_count;
    desc_write.descriptorType = binding.type;
    desc_write.pImageInfo = shader_binding.resources.images.data();
    desc_write.pBufferInfo = shader_binding.resources.buffers.data();
    desc_write.pTexelBufferView = nullptr;

    bind::StageBarriers& stage_barriers = registry.ComponentRequest<bind::StageBarriers>(_node);

    switch (binding.type)
    {
    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
        for (uint32_t desc_index = 0u; desc_index < descriptor_count; ++desc_index)
            stage_barriers.buffer_barriers[shader_binding.resources.buffers[desc_index].buffer].second = bind::AccessInfo{
                binding.stages,
                VK_ACCESS_UNIFORM_READ_BIT
            };
        break;

    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
        for (uint32_t desc_index = 0u; desc_index < descriptor_count; ++desc_index)
            stage_barriers.buffer_barriers[shader_binding.resources.buffers[desc_index].buffer].second = bind::AccessInfo{
                binding.stages,
                VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT
            };
        break;
    default:
        break;
    }

    return desc_write;
}

VkWriteDescriptorSet
DispatchContext::BindShaderResource(dstk::Registry::Node _node,
                                    layout::BindingPoint const& _location,
                                    uint32_t _invoc_index,
                                    bind::ShaderResources const& _resources)
{
    return BindShaderResource(_node, _location, std::vector<uint32_t>{ _invoc_index }, _resources);
}

} // namespace runtime

} // namespace vkrg
