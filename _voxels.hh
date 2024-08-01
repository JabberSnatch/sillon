#pragma once

#include "vktk.hh"
#include "loader/iotk.hpp"

namespace Voxels
{

struct Interface
{
    Interface();
    ~Interface();

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

}
