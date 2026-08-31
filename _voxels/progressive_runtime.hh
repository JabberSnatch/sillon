#pragma once

#include "dispatch_runtime.hh"

#include "gui_runtime.hh"
#include "voxel_field_runtime.hh"

namespace progressive_runtime
{

struct Args
{
    gui_runtime::Args gui;
    voxel_field_runtime::Args voxel_field;

    numtk::vec2u framebuffer_size;
    uint32_t swapchain_index;
    VkImage swapchain_image;
    uint32_t progressive_frame_index;
};

dispatch_runtime_t Create(vktk::Context* _vulkan, dstk::Registry* _registry);
void Destroy(dispatch_runtime_t&);

vkrg::layout::DispatchGraph RenderGraph(dstk::Registry& _registry);

} // namespace progressive_runtime

namespace progressive_runtime
{

struct Impl
{
    vktk::Texture primary_pass_color;
    vktk::Texture primary_pass_depth;
    vktk::Texture accumulated_color;
    VkSampler nearest_sampler;

    void OnGlobalInit(vkrg::runtime::DispatchContext _context);
    void OnSwapchainInit(vkrg::runtime::DispatchContext _context,
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

    {
        dstk::Registry::Node node = runtime.graph["FramebufferDispatch"]["SparseRegionSubpass"];
        dispatch_runtime_t runtime = voxel_field_runtime::Create(_vulkan, _registry);
        runtime.graph = *runtime.registry->ComponentLookup<vkrg::layout::DispatchGraph>(node);
        runtime.registry->BindComponent(node, runtime);
    }

    {
        dstk::Registry::Node node = runtime.graph["FramebufferScreenSpace"]["GUISubpass"];
        dispatch_runtime_t runtime = gui_runtime::Create(_vulkan, _registry);
        runtime.graph = *runtime.registry->ComponentLookup<vkrg::layout::DispatchGraph>(node);
        runtime.registry->BindComponent(node, runtime);
    }

    runtime.OnGlobalInit = [](dispatch_runtime_t& _rt){
        ((Impl*)_rt.impl)->OnGlobalInit(_rt.Context());
    };

    runtime.OnSwapchainInit = [](dispatch_runtime_t& _rt, vktk::Swapchain const& _swapchain){
        ((Impl*)_rt.impl)->OnSwapchainInit(_rt.Context(), _swapchain);
    };

    runtime.UpdateBindings = [](dispatch_runtime_t& _rt, dispatch_runtime_t::args_t _args){
        ((Impl*)_rt.impl)->UpdateBindings(_rt.Context(), *(Args const*)_args);
    };

    runtime.PostSubmit = [](dispatch_runtime_t& _rt, VkFence _fence){
        ((Impl*)_rt.impl)->PostSubmit(_rt.Context(), _fence);
    };

    return runtime;
}

void Destroy(dispatch_runtime_t& _rt)
{
    delete (Impl*)_rt.impl;
}

void Impl::OnGlobalInit(vkrg::runtime::DispatchContext _context)
{
    _context.GenerateStaticRuntime();
    _context.ResetDynamicRuntime();

    VkSamplerCreateInfo sampler_create{};
    sampler_create.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_create.pNext = nullptr;
    sampler_create.flags = 0u;
    sampler_create.magFilter = VK_FILTER_NEAREST;
    sampler_create.minFilter = VK_FILTER_NEAREST;
    sampler_create.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler_create.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_create.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_create.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_create.minLod = 0.0;
    sampler_create.maxLod = 0.0;
    CHECKCALL(vkCreateSampler, _context.vulkan.device,
              &sampler_create, nullptr, &nearest_sampler);

    _context.graph.DepthFirstTraversal(
        [&]
        (uint32_t, dstk::Registry::Node node) {
            dispatch_runtime_t* runtime =
                _context.registry.ComponentLookup<dispatch_runtime_t>(node);
            if (runtime && runtime->OnGlobalInit)
                runtime->OnGlobalInit(*runtime);
        });
}

void Impl::OnSwapchainInit(vkrg::runtime::DispatchContext _context,
                           vktk::Swapchain const& _swapchain)
{
    _context.ClearFramebufferRuntime();
    _context.GenerateFramebufferRuntime(&_swapchain.format);

    {
        _context.vulkan.DestroyTexture(primary_pass_color);
        primary_pass_color = _context.vulkan.CreateTexture(
            _swapchain.size.width, _swapchain.size.height, 1,
            VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
            | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
            | VK_IMAGE_USAGE_SAMPLED_BIT
        );

        _context.vulkan.DestroyTexture(primary_pass_depth);
        primary_pass_depth = _context.vulkan.CreateTexture(
            _swapchain.size.width, _swapchain.size.height, 1,
            VK_IMAGE_TYPE_2D, VK_FORMAT_D32_SFLOAT,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
        );

        _context.vulkan.DestroyTexture(accumulated_color);
        accumulated_color = _context.vulkan.CreateTexture(
            _swapchain.size.width, _swapchain.size.height, 1,
            VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
            | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
            | VK_IMAGE_USAGE_SAMPLED_BIT,
            {
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_COLOR_ATTACHMENT_READ_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
            },
            [&_swapchain](void* buffer) {
                std::memset(buffer, 0,
                            _swapchain.size.width * _swapchain.size.height * 16);
            }
        );
    }

    numtk::vec2u size = { _swapchain.size.width, _swapchain.size.height };
    _context.BindAttachments(
        _context.graph["FramebufferDispatch"], size,
        { primary_pass_color.view, primary_pass_depth.view }
    );

    _context.BindAttachments(
        _context.graph["FramebufferResolve"], size,
        { accumulated_color.view }
    );

    VkDescriptorImageInfo image_info{};
    image_info.sampler = nearest_sampler;
    image_info.imageView = primary_pass_color.view;
    image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet desc_write = _context.BindShaderResource(
        _context.graph["FramebufferResolve"]["ResolvePass"],
        vkrg::layout::BindingPoint{
            .type = vkrg::layout::BindingType_Shader,
            .shader = { 0, 0 }
        },
        0,
        vkrg::bind::ShaderResources{
            .images = { image_info }
        });

    vkUpdateDescriptorSets(_context.vulkan.device, 1, &desc_write, 0u, nullptr);

    _context.BindBlitCommand(
        _context.graph["FramebufferBlit"],
        vkrg::bind::BlitCommand{
            numtk::bounds3u{
                { 0, 0, 0 }, { _swapchain.size.width, _swapchain.size.height, 1 }
            },
            numtk::bounds3u{
                { 0, 0, 0 }, { _swapchain.size.width, _swapchain.size.height, 1 }
            },
            VK_IMAGE_ASPECT_COLOR_BIT,
            VK_FILTER_NEAREST
        }
    );

    _context.BindImage(
        _context.graph["FramebufferBlit"],
        vkrg::layout::BindingPoint{
            .type = vkrg::layout::BindingType_BlitImage,
            .blit_slot = vkrg::layout::BlitImageSlot_Source
        },
        accumulated_color.image
    );

    _context.BindSwapchain(_context.graph["FramebufferScreenSpace"], _swapchain);

    _context.graph.DepthFirstTraversal(
        [&]
        (uint32_t, dstk::Registry::Node node) {
            dispatch_runtime_t* runtime =
                _context.registry.ComponentLookup<dispatch_runtime_t>(node);
            if (runtime && runtime->OnSwapchainInit)
                runtime->OnSwapchainInit(*runtime, _swapchain);
        });
}

void Impl::UpdateBindings(vkrg::runtime::DispatchContext _context,
                          Args const& _args)
{
    {
        dispatch_runtime_t& runtime =
            *_context.registry.ComponentLookup<dispatch_runtime_t>(
                _context.graph["FramebufferDispatch"]["SparseRegionSubpass"]);
        runtime.UpdateBindings(runtime, &_args.voxel_field);
    }

    {
        dispatch_runtime_t& runtime =
            *_context.registry.ComponentLookup<dispatch_runtime_t>(
                _context.graph["FramebufferScreenSpace"]["GUISubpass"]);
        runtime.UpdateBindings(runtime, &_args.gui);
    }

    numtk::bounds2u scissor{ { 0, 0 },
                             { _args.framebuffer_size.x(), _args.framebuffer_size.y() } };
    numtk::bounds2f viewport{ { 0.f, 0.f },
                              { (float)scissor.extent.x(), (float)scissor.extent.y() } };

    _context.BindFramebufferArea(
        _context.graph["FramebufferDispatch"],
        { 0, scissor, viewport });

    _context.BindFramebufferArea(
        _context.graph["FramebufferResolve"],
        { 0, scissor, viewport });

    _context.BindImage(
        _context.graph["FramebufferBlit"],
        vkrg::layout::BindingPoint{
            .type = vkrg::layout::BindingType_BlitImage,
            .blit_slot = vkrg::layout::BlitImageSlot_Sink
        },
        _args.swapchain_image
    );

    _context.BindFramebufferArea(
        _context.graph["FramebufferScreenSpace"], {
            _args.swapchain_index, scissor, viewport
        }
    );

    _context.BindGraphicsCommand(
        _context.graph["FramebufferResolve"]["ResolvePass"],
        0,
        { vkrg::bind::DispatchCommand_Draw, 3, 1 });

    float* push_constants = (float*)_context.BindPushConstants(
        _context.graph["FramebufferResolve"]["ResolvePass"],
        {
            .type = vkrg::layout::BindingType_PushConstantRange,
            .push_constant_range = 0
        }, 0);
    *push_constants = 1.f / (float)(_args.progressive_frame_index + 1);
}

void Impl::PostSubmit(vkrg::runtime::DispatchContext _context,
                      VkFence _fence)
{
    _context.graph.DepthFirstTraversal(
        [&]
        (uint32_t, dstk::Registry::Node node) {
            dispatch_runtime_t* runtime =
                _context.registry.ComponentLookup<dispatch_runtime_t>(node);
            if (runtime && runtime->PostSubmit)
                runtime->PostSubmit(*runtime, _fence);
        });
}

vkrg::layout::DispatchStage ProgressiveResolveStageInfo()
{
    vkrg::layout::DispatchStage output = {};
    output.name = "progressive-resolve";

    output.shaders = std::vector<vkrg::layout::ShaderStage>{
        { VK_SHADER_STAGE_VERTEX_BIT,
          vktk::Context::CompileShader_GLSLtoSPIRV(
              VK_SHADER_STAGE_VERTEX_BIT, "progressive-resolve_vertex",
              { "#version 450",
                #include "shaders/full_screen.vert.h"
              }
          )
        },
        { VK_SHADER_STAGE_FRAGMENT_BIT,
          vktk::Context::CompileShader_GLSLtoSPIRV(
              VK_SHADER_STAGE_FRAGMENT_BIT, "progressive-resolve_fragment",
              { "#version 450",
                "layout(location = 0) in vec3 iFS_csPosition;"
                "layout(location = 0) out vec4 oFS_color;"
                "layout(set = 0, binding = 0) uniform sampler2D framebuffer;"
                "layout(push_constant, std430) uniform PushConstant {"
                "float sample_weight;"
                "};"
                "void main() {"
                "oFS_color = vec4(texelFetch(framebuffer, ivec2(gl_FragCoord.xy), 0).xyz, sample_weight);"
                "}"
              }
          )
        }
    };

    output.invocation_count = 1;
    output.binding_groups.push_back(vkrg::layout::StageBindingGroup{
        {
            { VK_SHADER_STAGE_FRAGMENT_BIT, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
              0, 1 }
        }
    });

    output.push_constants.push_back(vkrg::layout::PushConstantRange{
        VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(float) }
    );

    output.graphics.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    output.graphics.blend_states.push_back(vktk::Context::BlendState{
        VK_TRUE,
        VK_BLEND_FACTOR_SRC_ALPHA,
        VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        VK_BLEND_OP_ADD
    });

    return output;
}

vkrg::layout::DispatchGraph RenderGraph(dstk::Registry& _registry)
{
    vkrg::layout::DispatchGraph graph{ &_registry };

    _registry.BindComponent(graph["FramebufferDispatch"], vkrg::layout::DispatchPass{
        .mode = vkrg::layout::DispatchMode_Graphics,
        .graphics = {
            .attachments = {
                {
                    VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT,
                    numtk::vec4f{ 0.f, 1.f, 1.f, 0.f }
                },
                {
                    VK_IMAGE_TYPE_2D, VK_FORMAT_D32_SFLOAT,
                    vkrg::layout::ClearValue{ 1.f, 0u }
                }
            }
        }
    });

    {
        vkrg::layout::DispatchGraph& subpasses = graph.MakeSubgraph("FramebufferDispatch");
        subpasses.BindSubgraph("SparseRegionSubpass", voxel_field_runtime::RenderGraph(_registry));
    }

    _registry.BindComponent(graph["FramebufferResolve"], vkrg::layout::DispatchPass{
        .mode = vkrg::layout::DispatchMode_Graphics,
        .graphics = {
            .attachments = {
                { VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT,
                  numtk::vec4f{ 0.f, 0.f, 0.f, 0.f },
                  VK_ATTACHMENT_LOAD_OP_LOAD,
                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                }
            }
        }
    });
    graph.MakeSubgraph("FramebufferResolve");
    _registry.BindComponent(graph["FramebufferResolve"]["ResolvePass"],
                            ProgressiveResolveStageInfo());

    _registry.BindComponent(graph.MakeEdge("FramebufferDispatch", "FramebufferResolve"),
                            vkrg::layout::EdgeBindings{
                                vkrg::layout::EdgeType_Image,
                                vkrg::layout::BindingPoint{
                                    .type = vkrg::layout::BindingType_Framebuffer,
                                    .attachment_index = 0
                                },
                                vkrg::layout::BindingPoint{
                                    .type = vkrg::layout::BindingType_Shader,
                                    .node_override = graph["FramebufferResolve"]["ResolvePass"],
                                    .shader = { 0, 0 }
                                }
                            });

    _registry.BindComponent(graph["FramebufferBlit"], vkrg::layout::NodeTag_BlitImage);
    _registry.BindComponent(graph.MakeEdge("FramebufferResolve", "FramebufferBlit"),
                            vkrg::layout::EdgeBindings{
                                vkrg::layout::EdgeType_Image,
                                vkrg::layout::BindingPoint{
                                    .type = vkrg::layout::BindingType_Framebuffer,
                                    .attachment_index = 0
                                },
                                vkrg::layout::BindingPoint{
                                    .type = vkrg::layout::BindingType_BlitImage,
                                    .blit_slot = vkrg::layout::BlitImageSlot_Source
                                }
                            });

    _registry.BindComponent(graph.MakeWeakEdge("FramebufferBlit", "FramebufferResolve"),
                            vkrg::layout::EdgeBindings{
                                vkrg::layout::EdgeType_Image,
                                vkrg::layout::BindingPoint{
                                    .type = vkrg::layout::BindingType_BlitImage,
                                    .blit_slot = vkrg::layout::BlitImageSlot_Source
                                },
                                vkrg::layout::BindingPoint{
                                    .type = vkrg::layout::BindingType_Framebuffer,
                                    .attachment_index = 0
                                }
                            });

    _registry.BindComponent(graph["swapchain_input"], vkrg::layout::NodeTag_Input);
    _registry.BindComponent(graph.MakeEdge("swapchain_input", "FramebufferBlit"),
                            vkrg::layout::EdgeBindings{
                                vkrg::layout::EdgeType_Image,
                                {},
                                    vkrg::layout::BindingPoint{
                                    .type = vkrg::layout::BindingType_BlitImage,
                                        .blit_slot = vkrg::layout::BlitImageSlot_Sink
                                        }
                            });

    _registry.BindComponent(graph["FramebufferScreenSpace"], vkrg::layout::DispatchPass{
        .mode = vkrg::layout::DispatchMode_Graphics,
        .graphics = {
            .attachments = {
                {
                    VK_IMAGE_TYPE_2D, VK_FORMAT_B8G8R8A8_SRGB,
                    numtk::vec4f::Constant(0.f), VK_ATTACHMENT_LOAD_OP_LOAD
                },
                {
                    VK_IMAGE_TYPE_2D, VK_FORMAT_D32_SFLOAT,
                    vkrg::layout::ClearValue{ 1.f, 0u }
                }
            }
        }
    });

    {
        vkrg::layout::DispatchGraph& subpasses = graph.MakeSubgraph("FramebufferScreenSpace");
        subpasses.BindSubgraph("GUISubpass", gui_runtime::RenderGraph(_registry));
    }

    _registry.BindComponent(graph.MakeEdge("FramebufferBlit", "FramebufferScreenSpace"),
                            vkrg::layout::EdgeBindings{
                                vkrg::layout::EdgeType_Image,
                                vkrg::layout::BindingPoint{
                                    .type = vkrg::layout::BindingType_BlitImage,
                                    .blit_slot = vkrg::layout::BlitImageSlot_Sink
                                },
                                vkrg::layout::BindingPoint{
                                    .type = vkrg::layout::BindingType_Framebuffer,
                                    .attachment_index = 0
                                }
                            });

    _registry.BindComponent(graph["present"], vkrg::layout::NodeTag_Present);
    _registry.BindComponent(graph.MakeEdge("FramebufferScreenSpace", "present"),
                            vkrg::layout::EdgeBindings{
                                vkrg::layout::EdgeType_Image,
                                vkrg::layout::BindingPoint{
                                    .type = vkrg::layout::BindingType_Framebuffer,
                                    .attachment_index = 0
                                },
                                {}
                            });

    return graph;
}

} // namespace progressive_runtime
