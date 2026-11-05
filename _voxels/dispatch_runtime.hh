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

    void Dispatch(dispatch_runtime_t& _rt, args_t _args,
                  vktk::CommandBuffer& _command_buffer)
    {
        _rt.UpdateBindings(_rt, _args);

        _rt.Context().ScopeOpenFramebuffers(dispatch_index);
        _rt.Context().Dispatch(_command_buffer, dispatch_index);

        _command_buffer.post_submit_tasks.emplace_back([index = dispatch_index,
                                                        context = _rt.Context()]
                                                       (vktk::Context&) mutable {
            context.ScopeCloseFramebuffers(index);
        });

        ++dispatch_index;
    }

    vkrg::layout::DispatchGraph graph;
    vktk::Context* vulkan;
    dstk::Registry* registry;
    uint32_t dispatch_index = 0;
    impl_t impl;

    void (*Destroy)(dispatch_runtime_t&);

    void (*OnModuleScopeOpen)(dispatch_runtime_t&);
    void (*OnModuleScopeClose)(dispatch_runtime_t&);

    void (*OnSwapchainScopeOpen)(dispatch_runtime_t&, vktk::Swapchain const&);
    void (*OnSwapchainScopeClose)(dispatch_runtime_t&);

    void (*UpdateBindings)(dispatch_runtime_t&, args_t);
    void (*PostSubmit)(dispatch_runtime_t&, VkFence);
};
