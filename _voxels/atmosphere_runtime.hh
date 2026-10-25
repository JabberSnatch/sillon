#pragma once

#include "dispatch_runtime.hh"

namespace atmosphere_runtime
{

struct Args
{
    numtk::vec2i transmittance_size;
    vktk::Texture transmittance_texture;

    numtk::vec2i multi_scattering_size;
    vktk::Texture multi_scattering_texture;

    numtk::vec2i irradiance_size;
    vktk::Texture irradiance_texture;
    vktk::Texture baseline_texture;

    numtk::vec3f light_dir;
    float altitude;
};

dispatch_runtime_t Create(vktk::Context* _vulkan, dstk::Registry* _registry);

} // namespace atmosphere_runtime

namespace atmosphere_runtime
{

vkrg::layout::DispatchGraph RenderGraph(dstk::Registry& _registry);

struct TransmittancePushConstants
{
    numtk::vec2i render_size;
};

struct IrradiancePushConstants
{
    numtk::vec3f light_dir;
    float altitude;
    numtk::vec2i render_size;
};

struct MultiScatteringPushConstants
{
    numtk::vec2i render_size;
};

struct Impl
{
    VkSampler linear_sampler;

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
    vkDestroySampler(_context.vulkan.device, linear_sampler, nullptr);
}

void Impl::OnModuleScopeOpen(vkrg::runtime::DispatchContext _context)
{
    _context.GenerateStaticRuntime();
    _context.ResetDynamicRuntime();
    _context.ClearFramebufferRuntime();
    _context.GenerateFramebufferRuntime();

    VkSamplerCreateInfo sampler_create{};
    sampler_create.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_create.pNext = nullptr;
    sampler_create.flags = 0u;
    sampler_create.magFilter = VK_FILTER_LINEAR;
    sampler_create.minFilter = VK_FILTER_LINEAR;
    sampler_create.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler_create.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_create.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_create.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_create.minLod = 0.0;
    sampler_create.maxLod = 0.0;
    CHECKCALL(vkCreateSampler, _context.vulkan.device,
              &sampler_create, nullptr, &linear_sampler);
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
            _context.graph["TrPass"], _args.transmittance_size.cast<uint32_t>(),
            { _args.transmittance_texture.view }
        );

        numtk::bounds2u scissor{ { 0, 0 },
            _args.transmittance_size.cast<uint32_t>() };
        numtk::bounds2f viewport{ { 0.f, 0.f },
            _args.transmittance_size.cast<float>() };
        _context.BindFramebufferArea(
            _context.graph["TrPass"],
            { 0, scissor, viewport }
        );

        dstk::Registry::Node stage_node =
            _context.graph["TrPass"]["TrStage"];
        vkrg::bind::GraphicsCommand command = {};
        command.command = vkrg::bind::DispatchCommand_Draw;
        command.vertex_count = 3;
        command.instance_count = 1;
        _context.BindGraphicsCommand(stage_node, 0, command);

        TransmittancePushConstants* push_constants =
            (TransmittancePushConstants*)_context.BindPushConstants(
                stage_node,
                {
                    .type = vkrg::layout::BindingType_PushConstantRange,
                    .push_constant_range = 0
                }, 0);
        push_constants->render_size = _args.transmittance_size;
    }

    {
        _context.BindAttachments(
            _context.graph["MsPass"], _args.transmittance_size.cast<uint32_t>(),
            { _args.multi_scattering_texture.view }
        );

        numtk::bounds2u scissor{ { 0, 0 },
            _args.multi_scattering_size.cast<uint32_t>() };
        numtk::bounds2f viewport{ { 0.f, 0.f },
            _args.multi_scattering_size.cast<float>() };
        _context.BindFramebufferArea(
            _context.graph["MsPass"],
            { 0, scissor, viewport }
        );

        dstk::Registry::Node stage_node =
            _context.graph["MsPass"]["MsStage"];

        {
            VkDescriptorImageInfo image_info{};
            image_info.sampler = linear_sampler;
            image_info.imageView = _args.transmittance_texture.view;
            image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            VkWriteDescriptorSet desc_write = _context.BindShaderResource(
                stage_node,
                vkrg::layout::BindingPoint{
                    .type = vkrg::layout::BindingType_Shader,
                    .shader = { 0, 0 }
                },
                0,
                vkrg::bind::ShaderResources{
                    .images = { image_info }
                });

            vkUpdateDescriptorSets(_context.vulkan.device, 1, &desc_write, 0u, nullptr);
        }

        vkrg::bind::GraphicsCommand command = {};
        command.command = vkrg::bind::DispatchCommand_Draw;
        command.vertex_count = 3;
        command.instance_count = 1;
        _context.BindGraphicsCommand(stage_node, 0, command);

        MultiScatteringPushConstants* push_constants =
            (MultiScatteringPushConstants*)_context.BindPushConstants(
                stage_node,
                {
                    .type = vkrg::layout::BindingType_PushConstantRange,
                    .push_constant_range = 0
                }, 0);
        push_constants->render_size = _args.multi_scattering_size;
    }

    {
        _context.BindAttachments(
            _context.graph["IrPass"], _args.irradiance_size.cast<uint32_t>(),
            { _args.irradiance_texture.view, _args.baseline_texture.view }
        );

        numtk::bounds2u scissor{ { 0, 0 },
            _args.irradiance_size.cast<uint32_t>() };
        numtk::bounds2f viewport{ { 0.f, 0.f },
            _args.irradiance_size.cast<float>() };
        _context.BindFramebufferArea(
            _context.graph["IrPass"],
            { 0, scissor, viewport }
        );

        dstk::Registry::Node stage_node =
            _context.graph["IrPass"]["IrStage"];

        {
            VkDescriptorImageInfo image_info{};
            image_info.sampler = linear_sampler;
            image_info.imageView = _args.transmittance_texture.view;
            image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            VkWriteDescriptorSet desc_write = _context.BindShaderResource(
                stage_node,
                vkrg::layout::BindingPoint{
                    .type = vkrg::layout::BindingType_Shader,
                    .shader = { 0, 0 }
                },
                0,
                vkrg::bind::ShaderResources{
                    .images = { image_info }
                });

            vkUpdateDescriptorSets(_context.vulkan.device, 1, &desc_write, 0u, nullptr);
        }

        {
            VkDescriptorImageInfo image_info{};
            image_info.sampler = linear_sampler;
            image_info.imageView = _args.multi_scattering_texture.view;
            image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            VkWriteDescriptorSet desc_write = _context.BindShaderResource(
                stage_node,
                vkrg::layout::BindingPoint{
                    .type = vkrg::layout::BindingType_Shader,
                    .shader = { 0, 1 }
                },
                0,
                vkrg::bind::ShaderResources{
                    .images = { image_info }
                });

            vkUpdateDescriptorSets(_context.vulkan.device, 1, &desc_write, 0u, nullptr);
        }

        vkrg::bind::GraphicsCommand command = {};
        command.command = vkrg::bind::DispatchCommand_Draw;
        command.vertex_count = 3;
        command.instance_count = 1;
        _context.BindGraphicsCommand(stage_node, 0, command);

        IrradiancePushConstants* push_constants =
            (IrradiancePushConstants*)_context.BindPushConstants(
                stage_node,
                {
                    .type = vkrg::layout::BindingType_PushConstantRange,
                    .push_constant_range = 0
                }, 0);
        push_constants->render_size = _args.irradiance_size;
        push_constants->light_dir = _args.light_dir;
        push_constants->altitude = _args.altitude;
    }
}

void Impl::PostSubmit(vkrg::runtime::DispatchContext _context,
                      VkFence _fence)
{
}

vkrg::layout::DispatchGraph RenderGraph(dstk::Registry& _registry)
{
    vkrg::layout::DispatchGraph output{ &_registry };

    _registry.BindComponent(output["TrPass"], vkrg::layout::DispatchPass{
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

    _registry.BindComponent(output["MsPass"], vkrg::layout::DispatchPass{
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

    _registry.BindComponent(output["IrPass"], vkrg::layout::DispatchPass{
        .mode = vkrg::layout::DispatchMode_Graphics,
        .graphics = {
            .attachments = {
                {
                    VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT,
                    numtk::vec4f::Constant(0.f)
                },
                {
                    VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT,
                    numtk::vec4f::Constant(0.f)
                },
            }
        }
    });

    {
        vkrg::layout::DispatchGraph& tr_pass = output.MakeSubgraph("TrPass");
        vkrg::layout::DispatchStage tr_stage = {};
        tr_stage.name = "transmittance";

        tr_stage.shaders = std::vector<vkrg::layout::ShaderStage>{
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

        tr_stage.invocation_count = 1;
        tr_stage.graphics.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        tr_stage.push_constants = std::vector<vkrg::layout::PushConstantRange>{
            { VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(TransmittancePushConstants) }
        };

        _registry.BindComponent(tr_pass["TrStage"], tr_stage);
    }

    {
        vkrg::layout::DispatchGraph& ms_pass = output.MakeSubgraph("MsPass");
        vkrg::layout::DispatchStage ms_stage = {};
        ms_stage.name = "high_order_irradiance";

        ms_stage.shaders = std::vector<vkrg::layout::ShaderStage>{
            { VK_SHADER_STAGE_VERTEX_BIT,
              vktk::Context::CompileShader_GLSLtoSPIRV(
                  VK_SHADER_STAGE_VERTEX_BIT, "full_screen_draw",
                  { "#version 450",
                    #include "../shaders/full_screen.vert.h"
                  }
              )
            },
            { VK_SHADER_STAGE_FRAGMENT_BIT, {
                    #include "shaders/atmosphere_high_order.frag.glsl.spv"
                }
            }
        };

        ms_stage.invocation_count = 1;
        ms_stage.graphics.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        ms_stage.push_constants = std::vector<vkrg::layout::PushConstantRange>{
            { VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(MultiScatteringPushConstants) }
        };

        ms_stage.binding_groups.push_back(vkrg::layout::StageBindingGroup{
            {
                { VK_SHADER_STAGE_FRAGMENT_BIT, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                  0, 1 }
            }
        });

        _registry.BindComponent(ms_pass["MsStage"], ms_stage);
    }

    {
        vkrg::layout::DispatchGraph& ir_pass = output.MakeSubgraph("IrPass");
        vkrg::layout::DispatchStage ir_stage = {};
        ir_stage.name = "first_order_irradiance";

        ir_stage.shaders = std::vector<vkrg::layout::ShaderStage>{
            { VK_SHADER_STAGE_VERTEX_BIT,
              vktk::Context::CompileShader_GLSLtoSPIRV(
                  VK_SHADER_STAGE_VERTEX_BIT, "full_screen_draw",
                  { "#version 450",
                    #include "../shaders/full_screen.vert.h"
                  }
              )
            },
            { VK_SHADER_STAGE_FRAGMENT_BIT, {
                    #include "shaders/atmosphere_first_order.frag.glsl.spv"
                }
            }
        };

        ir_stage.invocation_count = 1;
        ir_stage.graphics.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        ir_stage.push_constants = std::vector<vkrg::layout::PushConstantRange>{
            { VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(IrradiancePushConstants) }
        };

        ir_stage.binding_groups.push_back(vkrg::layout::StageBindingGroup{
            {
                { VK_SHADER_STAGE_FRAGMENT_BIT, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                  0, 1 },
                { VK_SHADER_STAGE_FRAGMENT_BIT, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                  0, 1 }
            }
        });

        _registry.BindComponent(ir_pass["IrStage"], ir_stage);
    }

    _registry.BindComponent(output.MakeEdge("TrPass", "MsPass"),
                            vkrg::layout::EdgeBindings{
                                vkrg::layout::EdgeType_Image,
                                vkrg::layout::BindingPoint{
                                    .type = vkrg::layout::BindingType_Framebuffer,
                                    .attachment_index = 0
                                },
                                vkrg::layout::BindingPoint{
                                    .type = vkrg::layout::BindingType_Shader,
                                    .node_override = output["MsPass"]["MsStage"],
                                    .shader = { 0, 0 }
                                }
                            });

    _registry.BindComponent(output.MakeEdge("MsPass", "IrPass"),
                            vkrg::layout::EdgeBindings{
                                vkrg::layout::EdgeType_Image,
                                vkrg::layout::BindingPoint{
                                    .type = vkrg::layout::BindingType_Framebuffer,
                                    .attachment_index = 0
                                },
                                vkrg::layout::BindingPoint{
                                    .type = vkrg::layout::BindingType_Shader,
                                    .node_override = output["IrPass"]["IrStage"],
                                    .shader = { 0, 1 }
                                }
                            });

    _registry.BindComponent(output.MakeEdge("TrPass", "IrPass"),
                            vkrg::layout::EdgeBindings{
                                vkrg::layout::EdgeType_Image,
                                vkrg::layout::BindingPoint{
                                    .type = vkrg::layout::BindingType_Framebuffer,
                                    .attachment_index = 0
                                },
                                vkrg::layout::BindingPoint{
                                    .type = vkrg::layout::BindingType_Shader,
                                    .node_override = output["IrPass"]["IrStage"],
                                    .shader = { 0, 0 }
                                }
                            });

    return output;
}

} // namespace atmosphere_runtime
