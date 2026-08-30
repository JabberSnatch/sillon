#pragma once

#include <dstk.hh>

#include "../vktk.hh"

struct dispatch_runtime_t
{
    using args_t = void const*;
    using impl_t = void*;

    vkrg::runtime::DispatchContext Context() {
        return vkrg::runtime::DispatchContext{
            *registry,
            *vulkan,
            graph
        };
    }

    void Dispatch(dispatch_runtime_t& _rt, void* _args,
                  vktk::CommandBuffer& _command_buffer)
    {
        _rt.UpdateBindings(_rt, _args);
        _rt.Context().Dispatch(_command_buffer);
    }

    vkrg::layout::DispatchGraph graph;
    vktk::Context* vulkan;
    dstk::Registry* registry;
    impl_t impl;

    void (*OnGlobalInit)(dispatch_runtime_t&);
    void (*OnSwapchainInit)(dispatch_runtime_t&, vktk::Swapchain const&);

    void (*UpdateBindings)(dispatch_runtime_t&, args_t);
    void (*PostSubmit)(dispatch_runtime_t&, VkFence);
};
