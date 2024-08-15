#include "vktk.hh"
#include "loader/iotk.hpp"
#include "loader/bstk.hpp"

#include "_glyphs.hh"
#include "_icosphere.hh"
#include "_voxels.hh"

#include <cstdio>
#include <cstdint>
#include <iostream>

#if defined(_WIN32)
#define DLLEXPORT __declspec(dllexport)
#elif defined(__unix__)
#define DLLEXPORT __attribute__((visibility("default")))
#else
#define DLLEXPORT
#endif

struct plugin_t
{
    void (*ConstantResources)(void* _state,
                              vktk::Context* _vulkan);
    void (*RenderResources)(void* _state,
                            vktk::Context* _vulkan,
                            vktk::Swapchain const& _swapchain);

    bool (*Update)(void* _state,
                   vktk::Context* _vulkan,
                   iotk::input_t const* _input,
                   iotk::input_t const* _lastInput);
    void (*Draw)(void* _state,
                 vktk::Context* _vulkan,
                 vktk::Swapchain const& _swapchain,
                 VkSemaphore _acquire_semaphore);

    void (*Shutdown)(void* _state, vktk::Context* _vulkan);

    void* state;
};

struct context_t
{
    bstk::OSWindow window;

    std::unique_ptr<vktk::Context> vulkan;
    vktk::Swapchain swapchain;
    iotk::input_t lastInput;

    std::unique_ptr<Glyphs::Interface> glyphs_plugin;
    std::unique_ptr<Icosphere::Interface> icosphere_plugin;
    std::unique_ptr<Voxels::Interface> voxels_plugin;
    plugin_t plugin;
};

static bool UpdateSwapchain(context_t* _context, uint32_t const _size[2])
{
    if (_context->swapchain.handle != VK_NULL_HANDLE
        && (_context->swapchain.size.width != _size[0]
            || _context->swapchain.size.height != _size[1]))
        _context->swapchain.Release();

    bool result = false;
    if (_context->swapchain.handle == VK_NULL_HANDLE)
    {
        _context->swapchain = _context->vulkan->CreateSwapchain(_size);
        result = true;
    }

    return result;
}

static void LoadPlugins(context_t* context)
{
#if 1
    context->voxels_plugin->ReloadPointers();
    context->plugin.ConstantResources = context->voxels_plugin->ConstantResources;
    context->plugin.RenderResources = context->voxels_plugin->RenderResources;
    context->plugin.Update = context->voxels_plugin->Update;
    context->plugin.Draw = context->voxels_plugin->Draw;
    context->plugin.Shutdown = context->voxels_plugin->Shutdown;
    context->plugin.state = context->voxels_plugin->state;
#endif

#if 0
    context->plugin.ConstantResources = context->glyphs_plugin->ConstantResources;
    context->plugin.RenderResources = context->glyphs_plugin->RenderResources;
    context->plugin.Update = context->glyphs_plugin->Update;
    context->plugin.Draw = context->glyphs_plugin->Draw;
    context->plugin.Shutdown = context->glyphs_plugin->Shutdown;
    context->plugin.state = context->glyphs_plugin->state;
#endif

#if 0
    context->plugin.ConstantResources = context->icosphere_plugin->ConstantResources;
    context->plugin.RenderResources = context->icosphere_plugin->RenderResources;
    context->plugin.Update = context->icosphere_plugin->Update;
    context->plugin.Draw = context->icosphere_plugin->Draw;
    context->plugin.Shutdown = context->icosphere_plugin->Shutdown;
    context->plugin.state = context->icosphere_plugin->state;
#endif

#if 0
    std::cout << "loaded plugin : " << std::endl << std::hex
              << "\t" << (intptr_t)context->plugin.ConstantResources << std::endl
              << "\t" << (intptr_t)context->plugin.RenderResources << std::endl
              << "\t" << (intptr_t)context->plugin.Update << std::endl
              << "\t" << (intptr_t)context->plugin.Draw << std::endl
              << "\t" << (intptr_t)context->plugin.Shutdown << std::endl
              << "\t" << (intptr_t)context->plugin.state << std::endl;
    std::cout << std::dec;
#endif
}

// geometry
// camera
// structured data
// serialisation

extern "C" {

    DLLEXPORT context_t* ModuleInterface_Create(bstk::OSWindow const* _window)
    {
        std::cout << "Create" << std::endl;
        context_t* context = new context_t{};
        context->window = *_window;
        context->vulkan.reset(new vktk::Context(_window->hinstance, _window->hwindow));

        context->glyphs_plugin.reset(new Glyphs::Interface());
        context->icosphere_plugin.reset(new Icosphere::Interface());
        context->voxels_plugin.reset(new Voxels::Interface());

        LoadPlugins(context);

        if (context->plugin.ConstantResources)
            context->plugin.ConstantResources(context->plugin.state,
                                              context->vulkan.get());

        UpdateSwapchain(context, context->window.size);
        if (context->plugin.RenderResources)
            context->plugin.RenderResources(context->plugin.state,
                                            context->vulkan.get(),
                                            context->swapchain);

        return context;
    }

    DLLEXPORT void ModuleInterface_Shutdown(context_t* _context)
    {
        std::cout << "Shutdown" << std::endl;

        _context->vulkan->WaitPendingDestroys();
        if (_context->plugin.Shutdown)
            _context->plugin.Shutdown(_context->plugin.state,
                                      _context->vulkan.get());

        _context->swapchain.Release();
        delete _context;
    }

    DLLEXPORT void ModuleInterface_Reload(context_t* _context)
    {
        std::cout << "Reload" << std::endl;

        _context->vulkan->WaitPendingDestroys();
        if (_context->plugin.Shutdown)
            _context->plugin.Shutdown(_context->plugin.state,
                                      _context->vulkan.get());

        _context->swapchain.Release();

#if 0
        _context->vulkan.release();
        _context->vulkan.reset(new vktk::Context(_context->window.hinstance,
                                                 _context->window.hwindow));
#endif

        LoadPlugins(_context);

        if (_context->plugin.ConstantResources)
            _context->plugin.ConstantResources(_context->plugin.state,
                                               _context->vulkan.get());

        UpdateSwapchain(_context, _context->window.size);
        if (_context->plugin.RenderResources)
            _context->plugin.RenderResources(_context->plugin.state,
                                             _context->vulkan.get(),
                                             _context->swapchain);
    }

    DLLEXPORT bool ModuleInterface_LogicUpdate(context_t* _context, iotk::input_t const* _input)
    {
        bool keep_running = true;

        if (_context->plugin.Update)
            keep_running = _context->plugin.Update(_context->plugin.state,
                                                   _context->vulkan.get(),
                                                   _input,
                                                   &_context->lastInput);

        _context->lastInput = *_input;

        return keep_running;
    }

    DLLEXPORT void ModuleInterface_DrawFrame(context_t* _context, bstk::OSWindow const* _window)
    {
        //std::cout << "DrawFrame" << std::endl;
        _context->window = *_window;
        if (UpdateSwapchain(_context, _window->size))
        {
            if (_context->plugin.RenderResources)
                _context->plugin.RenderResources(_context->plugin.state,
                                                 _context->vulkan.get(),
                                                 _context->swapchain);
        }

        VkSemaphore acquire_semaphore{};
        {
            VkSemaphoreCreateInfo semaphore_info{};
            semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            semaphore_info.pNext = nullptr;
            semaphore_info.flags = 0u;
            vkCreateSemaphore(_context->vulkan->device, &semaphore_info, nullptr, &acquire_semaphore);

            _context->swapchain.AcquireNext(acquire_semaphore);
        }

        if (_context->plugin.Draw)
            _context->plugin.Draw(_context->plugin.state,
                                  _context->vulkan.get(),
                                  _context->swapchain,
                                  acquire_semaphore);

        vkQueueWaitIdle(_context->vulkan->present_queue);
        vkDestroySemaphore(_context->vulkan->device, acquire_semaphore, nullptr);

        _context->vulkan->TryPendingDestroys();
    }
}
