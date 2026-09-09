#pragma once

#include "dispatch_runtime.hh"

namespace atmosphere_runtime
{

struct Args
{
    numtk::vec2i render_size;
    vktk::Texture output_texture;
};

dispatch_runtime_t Create(vktk::Context* _vulkan, dstk::Registry* _registry);

} // namespace atmosphere_runtime

namespace atmosphere_runtime
{

vkrg::layout::DispatchGraph RenderGraph(dstk::Registry& _registry);

struct AtmospherePrecomputePushConstants
{
    numtk::vec2i render_size;
};

struct Impl
{
    void Destroy(vkrg::runtime::DispatchContext _context);

    void OnModuleScopeOpen(vkrg::runtime::DispatchContext _context);
    void OnSwapchainScopeOpen(vkrg::runtime::DispatchContext _context,
                              vktk::Swapchain const& _swapchain);

    void UpdateBindings(vkrg::runtime::DispatchContext _context,
                        Args const& _args);
    void PostSubmit(vkrg::runtime::DispatchContext _context,
                    VkFence _fence);
};

dispatch_runtime_t Create(vktk::Context* _vulkan, dstk::Registry* _registry)
{
    dispatch_runtime_t runtime = {};
    runtime.vulkan = _vulkan;
    runtime.registry = _registry;
    runtime.graph = RenderGraph(*_registry);
    runtime.impl = new Impl{};

    runtime.Destroy = [](dispatch_runtime_t& _rt){
        ((Impl*)_rt.impl)->Destroy(_rt.Context());
        delete (Impl*)_rt.impl;
    };

    runtime.OnModuleScopeOpen = [](dispatch_runtime_t& _rt){
        ((Impl*)_rt.impl)->OnModuleScopeOpen(_rt.Context());
    };

    runtime.OnSwapchainScopeOpen = [](dispatch_runtime_t& _rt, vktk::Swapchain const& _swapchain){
        ((Impl*)_rt.impl)->OnSwapchainScopeOpen(_rt.Context(), _swapchain);
    };

    runtime.UpdateBindings = [](dispatch_runtime_t& _rt, dispatch_runtime_t::args_t _args){
        ((Impl*)_rt.impl)->UpdateBindings(_rt.Context(), *(Args const*)_args);
    };

    runtime.PostSubmit = [](dispatch_runtime_t& _rt, VkFence _fence){
        ((Impl*)_rt.impl)->PostSubmit(_rt.Context(), _fence);
    };

    return runtime;
}

void Impl::Destroy(vkrg::runtime::DispatchContext _context)
{
}

void Impl::OnModuleScopeOpen(vkrg::runtime::DispatchContext _context)
{
    _context.GenerateStaticRuntime();
    _context.ResetDynamicRuntime();
    _context.ClearFramebufferRuntime();
    _context.GenerateFramebufferRuntime(nullptr);
}

void Impl::OnSwapchainScopeOpen(vkrg::runtime::DispatchContext _context,
                                vktk::Swapchain const& _swapchain)
{
}

void Impl::UpdateBindings(vkrg::runtime::DispatchContext _context,
                          Args const& _args)
{
    {
        _context.BindAttachments(
            _context.graph["FramebufferDispatch"], _args.render_size.cast<uint32_t>(),
            { _args.output_texture.view }
        );

        numtk::bounds2u scissor{ { 0, 0 },
            _args.render_size.cast<uint32_t>() };
        numtk::bounds2f viewport{ { 0.f, 0.f },
            _args.render_size.cast<float>() };
        _context.BindFramebufferArea(
            _context.graph["FramebufferDispatch"],
            { 0, scissor, viewport }
        );

        dstk::Registry::Node main_stage_node =
            _context.graph["FramebufferDispatch"]["Transmittance"];
        vkrg::bind::GraphicsCommand command = {};
        command.command = vkrg::bind::DispatchCommand_Draw;
        command.vertex_count = 3;
        command.instance_count = 1;
        _context.BindGraphicsCommand(main_stage_node, 0, command);

        AtmospherePrecomputePushConstants* push_constants =
            (AtmospherePrecomputePushConstants*)_context.BindPushConstants(
                main_stage_node,
                {
                    .type = vkrg::layout::BindingType_PushConstantRange,
                    .push_constant_range = 0
                }, 0);
        push_constants->render_size = _args.render_size;
    }
}

void Impl::PostSubmit(vkrg::runtime::DispatchContext _context,
                      VkFence _fence)
{
}

vkrg::layout::DispatchGraph RenderGraph(dstk::Registry& _registry)
{
    vkrg::layout::DispatchGraph output{ &_registry };
    _registry.BindComponent(output["FramebufferDispatch"], vkrg::layout::DispatchPass{
        .mode = vkrg::layout::DispatchMode_Graphics,
        .graphics = {
            .attachments = {
                {
                    VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT,
                    numtk::vec4f::Constant(0.f)
                }
            }
        }
    });

    vkrg::layout::DispatchGraph& subpasses = output.MakeSubgraph("FramebufferDispatch");

    vkrg::layout::DispatchStage main_stage = {};
    main_stage.name = "transmittance";

    main_stage.shaders = std::vector<vkrg::layout::ShaderStage>{
        { VK_SHADER_STAGE_VERTEX_BIT,
          vktk::Context::CompileShader_GLSLtoSPIRV(
              VK_SHADER_STAGE_VERTEX_BIT, "full_screen_draw",
              { "#version 450",
                #include "../shaders/full_screen.vert.h"
              }
          )
        },
        { VK_SHADER_STAGE_FRAGMENT_BIT, {
            #include "shaders/atmosphere_transmittance.frag.glsl.spv"
          }
        }
    };

    main_stage.invocation_count = 1;
    main_stage.graphics.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    main_stage.push_constants = std::vector<vkrg::layout::PushConstantRange>{
        { VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(AtmospherePrecomputePushConstants) }
    };

    _registry.BindComponent(subpasses["Transmittance"], main_stage);
    return output;
}

} // namespace atmosphere_runtime
