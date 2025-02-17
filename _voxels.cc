#include "_voxels.hh"

#define ZIPTK_IMPLEMENTATION
#define MCTK_IMPLEMENTATION
#include "mctk/mctk.hh"

#include "systk.hh"
#include "numtk.hh"
#include "vktk.hh"
#include "voxtk.hh"
#include "dstk.hh"

#include <bit>
#include <memory>
#include <unordered_map>

//#define TRAVERSAL_V1
#define TRAVERSAL_V2

namespace {

struct RegionData
{
    struct SectionData
    {
        std::vector<std::string> palette;
        std::array<uint16_t, 4096> block_ids;
    };

    struct ChunkData
    {
        std::string status;
        std::array<SectionData, 24> sections;
    };

    std::vector<ChunkData> chunks;
};

struct TurntableCamera
{
    numtk::vec3f spherical_coordinates = { 0.f, 0.f, 3.f };
    float y_offset = 0.f;
    numtk::vec3<int32_t> current_section = { 16, 8, 16 };

    void Update(iotk::input_t const* _input,
                iotk::input_t const* _past_input);
    numtk::mat4f Matrix() const;
};

struct FlightCamera
{
#if defined(TRAVERSAL_V1)
    numtk::vec2f forward_spherical = { .5f, -0.25f };
    numtk::vec3f position = { 16.f, -0.3f, 16.f };
#elif defined(TRAVERSAL_V2)
    numtk::vec2f forward_spherical = { 0.f, 0.f };
    numtk::vec3f position = { 0.f, 512.f, 0.f };
#endif

    float velocity = 5.f;

    void Update(iotk::input_t const* _input,
                iotk::input_t const* _past_input);
    numtk::mat4f Matrix() const;
};

struct DispatchStageInfo
{
    std::string name;

    // shaders
    struct ShaderStage {
        VkShaderStageFlagBits stage;
        std::vector<std::string> source;
    };
    std::vector<ShaderStage> shaders;

    // resource bindings
    struct BindingInfo {
        VkShaderStageFlags stages;
        VkDescriptorType type;
        VkDescriptorBindingFlags flags;
        uint32_t count;
    };
    struct BindingGroupInfo {
        std::vector<BindingInfo> bindings;
    };
    uint32_t group_instance_count;
    std::vector<BindingGroupInfo> binding_groups;

    // push constants
    struct PushConstantRange {
        VkShaderStageFlags stages;
        uint32_t offset;
        uint32_t size;
    };
    std::vector<PushConstantRange> push_constants;

    struct GraphicsInfo {
        std::vector<vktk::Context::StateOverride> state_overrides;

        // geometry attachments
        VkPrimitiveTopology topology;
        std::vector<vktk::Context::VertexInput> input_layout;
    };

    GraphicsInfo graphics;
};

struct DispatchNodeInfo
{
    std::string name;

    // dispatch description
    enum DispatchMode {
        DispatchMode_Graphics,
        DispatchMode_Compute,
    };
    DispatchMode mode;

    std::vector<DispatchStageInfo> sequence;

    // render attachments
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
    struct GraphicsInfo {
        std::vector<RenderAttachment> attachments;
    };
    GraphicsInfo graphics;
};

struct DispatchStageStatic
{
    struct BindingGroup {
        vktk::DescriptorPool pool;
        std::vector<VkDescriptorSet> sets;
    };
    VkPipelineLayout pipeline_layout;
    std::vector<BindingGroup> binding_groups;
};

struct DispatchNodeStatic
{
    std::vector<DispatchStageStatic> stages;
};

struct DispatchNodeFramebuffer
{
    struct GraphicsRuntime {
        VkRenderPass render_pass;
    };

    std::vector<VkPipeline> stages;
    GraphicsRuntime graphics;
};

struct ShaderBindingInfo
{
    uint32_t group_index;
    uint32_t binding_index;
};

struct FramebufferBindingInfo
{
    uint32_t attachment_index;
};

struct PushConstantsBindingInfo
{
    uint32_t range_index;
};

struct NodeBindingPoint
{
    DispatchNodeInfo const* node;
    uint32_t stage_index;

    enum BindingType {
        BindingType_Shader,
        BindingType_Framebuffer,
        BindingType_PushConstantRange,
        BindingType_VertexInput,
        BindingType_Present,
    };

    BindingType type;
    union {
        ShaderBindingInfo shader;
        FramebufferBindingInfo framebuffer;
        PushConstantsBindingInfo push_constants;
    };
};

struct GraphEdge
{
    enum EdgeType {
        EdgeType_Image,
        EdgeType_Buffer,
    };

    EdgeType type;
    NodeBindingPoint source;
    NodeBindingPoint target;
};

struct BindingPointSync
{
    VkPipelineStageFlags stage;
    VkAccessFlags access;
    VkImageLayout layout;
};

BindingPointSync BindingPointSyncRequirements(GraphEdge::EdgeType type, NodeBindingPoint const& binding_point)
{
    BindingPointSync output = {};
    output.access = VK_ACCESS_NONE;
    output.layout = VK_IMAGE_LAYOUT_UNDEFINED;
    output.stage = 0;

    if (type == GraphEdge::EdgeType_Image)
    {
        switch (binding_point.type)
        {

        case NodeBindingPoint::BindingType_Shader:
        {
            ShaderBindingInfo const& location = binding_point.shader;
            DispatchStageInfo const& stage_info =
                binding_point.node->sequence[binding_point.stage_index];
            DispatchStageInfo::BindingInfo const& binding = stage_info
                .binding_groups[location.group_index]
                .bindings[location.binding_index];

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

        case NodeBindingPoint::BindingType_Framebuffer:
        {
            FramebufferBindingInfo const& location = binding_point.framebuffer;
            DispatchNodeInfo::RenderAttachment const& attachment_info =
                binding_point.node->graphics.attachments[location.attachment_index];

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

        case NodeBindingPoint::BindingType_Present:
        {
            output.access = 0;
            output.layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        } break;

        default: break;
        }
    }

    return output;
}

struct DispatchGraph
{
    std::vector<DispatchNodeInfo> nodes;

    std::vector<GraphEdge> edges;
    std::unordered_map<DispatchNodeInfo const*, std::vector<uint32_t>> edges_in;
    std::unordered_map<DispatchNodeInfo const*, std::vector<uint32_t>> edges_out;

    void InsertEdge(GraphEdge const& edge)
    {
        uint32_t index = edges.size();
        edges.push_back(edge);
        GraphEdge const* pEdge = &edges.back();
        if (pEdge->source.node)
            edges_out[pEdge->source.node].push_back(index);
        if (pEdge->target.node)
            edges_in[pEdge->target.node].push_back(index);
    }

    std::vector<vktk::Context::RenderPassAttachment> RenderPassDescription(
        uint32_t _node_index,
        VkFormat const* _swapchain_format = nullptr) const
    {
        std::vector<vktk::Context::RenderPassAttachment> output = {};

        DispatchNodeInfo const& node = nodes[_node_index];
        for (uint32_t index = 0u; index < (uint32_t)node.graphics.attachments.size(); ++index)
        {
            NodeBindingPoint binding_point = {};
            binding_point.node = &node;
            binding_point.stage_index = -1;
            binding_point.type = NodeBindingPoint::BindingType_Framebuffer;
            binding_point.framebuffer.attachment_index = index;
            BindingPointSync sync_reqs = BindingPointSyncRequirements(GraphEdge::EdgeType_Image,
                                                                      binding_point);

            output.push_back(vktk::Context::RenderPassAttachment{
                node.graphics.attachments[index].format,
                VK_IMAGE_LAYOUT_UNDEFINED,
                sync_reqs.layout
            });
        }

        for (uint32_t edge_index : (edges_out.count(&node) ? edges_out.at(&node) : std::vector<uint32_t>{}))
        {
            GraphEdge const& edge = edges[edge_index];
            if (edge.source.type != NodeBindingPoint::BindingType_Framebuffer)
                continue;

            vktk::Context::RenderPassAttachment& desc = output[edge.source.framebuffer.attachment_index];
            BindingPointSync sync_reqs = BindingPointSyncRequirements(edge.type, edge.target);
            desc.finalLayout = sync_reqs.layout;

            if (_swapchain_format && edge.target.type == NodeBindingPoint::BindingType_Present)
                desc.format = *_swapchain_format;
        }

        for (uint32_t edge_index : (edges_in.count(&node) ? edges_in.at(&node) : std::vector<uint32_t>{}))
        {
            GraphEdge const& edge = edges[edge_index];
            if (edge.target.type != NodeBindingPoint::BindingType_Framebuffer)
                continue;

            vktk::Context::RenderPassAttachment& desc = output[edge.source.framebuffer.attachment_index];
            BindingPointSync sync_reqs = BindingPointSyncRequirements(edge.type, edge.source);
            desc.initialLayout = sync_reqs.layout;
        }

        return output;
    }
};

struct ShaderResourceBinding
{
    uint32_t descriptor_count;
    union {
        VkDescriptorImageInfo const* images;
        VkDescriptorBufferInfo const* buffers;
    };
};

struct GraphicsCommandBinding
{
    enum DispatchCommand
    {
        DispatchCommand_Draw,
        DispatchCommand_DrawIndexed,
    };

    DispatchCommand command;
    uint32_t vertex_count;
    uint32_t instance_count;
};

struct FramebufferAreaBinding
{
    uint32_t framebuffer_index;
    numtk::bounds2u scissor;
    numtk::bounds2f viewport;
};

struct DispatchStageBindings
{
    struct DispatchInstance {
        GraphicsCommandBinding graphics;
        std::vector<std::unique_ptr<uint8_t[]>> push_constants;
    };
    std::vector<DispatchInstance> instances;
};

struct DispatchNodeBindings
{
    FramebufferAreaBinding area_binding;
    std::vector<VkFramebuffer> framebuffers;
    std::vector<DispatchStageBindings> stages;
};

struct DispatchRuntime
{
    vktk::Context* vulkan;
    DispatchGraph const* graph;
    std::vector<DispatchNodeStatic> nodes_static;
    std::vector<DispatchNodeFramebuffer> nodes_framebuffer;
    std::vector<DispatchNodeBindings> nodes_bindings;

    void ClearStaticRuntime()
    {
        for (DispatchNodeStatic& node_runtime : nodes_static)
        {
            for (DispatchStageStatic& stage_runtime : node_runtime.stages)
            {
                vkDestroyPipelineLayout(vulkan->device, stage_runtime.pipeline_layout, nullptr);
                stage_runtime.pipeline_layout = VK_NULL_HANDLE;

                for (DispatchStageStatic::BindingGroup& group : stage_runtime.binding_groups)
                {
                    vkFreeDescriptorSets(vulkan->device,
                                         group.pool.pool,
                                         group.sets.size(),
                                         group.sets.data());
                    group.sets.clear();

                    vulkan->DestroyDescriptorPool(group.pool);
                    group.pool.pool = VK_NULL_HANDLE;
                    group.pool.layout = VK_NULL_HANDLE;
                }
            }
        }
    }

    void ClearFramebufferRuntime()
    {
        for (DispatchNodeBindings& bindings : nodes_bindings)
        {
            for (VkFramebuffer framebuffer : bindings.framebuffers)
                vkDestroyFramebuffer(vulkan->device, framebuffer, nullptr);
            bindings.framebuffers.clear();
        }

        for (DispatchNodeFramebuffer& runtime : nodes_framebuffer)
        {
            for (VkPipeline pipeline : runtime.stages)
                vkDestroyPipeline(vulkan->device, pipeline, nullptr);
            runtime.stages.clear();

            vkDestroyRenderPass(vulkan->device, runtime.graphics.render_pass, nullptr);
            runtime.graphics.render_pass = VK_NULL_HANDLE;
        }
        nodes_framebuffer.clear();
    }

    void GenerateFramebufferRuntime(VkFormat const* _swapchain_format = nullptr)
    {
        for (uint32_t node_index = 0; node_index < (uint32_t)graph->nodes.size(); ++node_index)
        {
            DispatchNodeInfo const& node = graph->nodes[node_index];
            DispatchNodeStatic const& node_static = nodes_static[node_index];
            DispatchNodeFramebuffer framebuffer_runtime = {};

            if (node.mode == DispatchNodeInfo::DispatchMode_Graphics)
                framebuffer_runtime.graphics.render_pass =
                    vulkan->CreateRenderPass(
                        graph->RenderPassDescription(node_index, _swapchain_format));

            for (uint32_t stage_index = 0; stage_index < (uint32_t)node.sequence.size(); ++stage_index)
            {
                DispatchStageInfo const& stage = node.sequence[stage_index];
                DispatchStageStatic const& runtime = node_static.stages[stage_index];

                std::vector<vktk::Context::PipelineStage> pipeline_stages = {};
                for (DispatchStageInfo::ShaderStage const& shader : stage.shaders)
                {
                    std::string shader_name = stage.name;
                    switch (shader.stage)
                    {
                    case VK_SHADER_STAGE_VERTEX_BIT: shader_name += "_vertex"; break;
                    case VK_SHADER_STAGE_FRAGMENT_BIT: shader_name += "_fragment"; break;
                    default: shader_name += "_unknown"; break;
                    }

                    VkShaderModule shader_module =
                        vulkan->CompileShader_GLSL(shader.stage, shader_name.c_str(), shader.source);

                    pipeline_stages.push_back(vktk::Context::PipelineStage{ shader_module, shader.stage });
                }

                if (node.mode == DispatchNodeInfo::DispatchMode_Graphics)
                {
                    VkPipeline pipeline = vulkan->CreatePipeline(
                        runtime.pipeline_layout,
                        framebuffer_runtime.graphics.render_pass,
                        pipeline_stages,
                        stage.graphics.state_overrides,
                        stage.graphics.topology,
                        stage.graphics.input_layout
                        );

                    framebuffer_runtime.stages.push_back(pipeline);
                }

                for (vktk::Context::PipelineStage const& pipeline_stage : pipeline_stages)
                    vkDestroyShaderModule(vulkan->device, pipeline_stage.module, nullptr);
            }

            nodes_framebuffer.push_back(framebuffer_runtime);
        }
    }

    void Dispatch(vktk::CommandBuffer& _command_buffer)
    {
        for (uint32_t node_index = 0u;
             node_index < (uint32_t)graph->nodes.size();
             ++node_index)
        {
            DispatchNodeInfo const& node_info = graph->nodes[node_index];
            DispatchNodeBindings const& bindings = nodes_bindings[node_index];
            DispatchNodeFramebuffer const& framebuffer = nodes_framebuffer[node_index];
            DispatchNodeStatic const& runtime = nodes_static[node_index];

            DispatchNodeInfo::GraphicsInfo const& graphics_info = node_info.graphics;

            std::vector<VkClearValue> clear_values = {};
            for(DispatchNodeInfo::RenderAttachment const& attachment : graphics_info.attachments)
            {
                VkClearValue clear_value = {};
                std::memcpy(&clear_value, &attachment.clear_value, sizeof(VkClearValue));
                clear_values.push_back(clear_value);
            }

            VkRect2D scissor{
                VkOffset2D{
                    (int32_t)bindings.area_binding.scissor.min.x(),
                    (int32_t)bindings.area_binding.scissor.min.y()
                },
                VkExtent2D{
                    bindings.area_binding.scissor.extent.x(),
                    bindings.area_binding.scissor.extent.y()
                }
            };

            VkViewport viewport{
                bindings.area_binding.viewport.min.x(),
                bindings.area_binding.viewport.min.y(),
                bindings.area_binding.viewport.extent.x(),
                bindings.area_binding.viewport.extent.y(),
                0.f, 1.f
            };

            vkCmdSetViewport(_command_buffer, 0, 1, &viewport);
            vkCmdSetScissor(_command_buffer, 0, 1, &scissor);

            VkRenderPassBeginInfo rp_begin_info{};
            rp_begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            rp_begin_info.pNext = nullptr;
            rp_begin_info.renderPass = framebuffer.graphics.render_pass;
            rp_begin_info.framebuffer = bindings.framebuffers[bindings.area_binding.framebuffer_index];
            rp_begin_info.renderArea = scissor;
            rp_begin_info.clearValueCount = graphics_info.attachments.size();
            rp_begin_info.clearValueCount = clear_values.size();
            rp_begin_info.pClearValues = clear_values.data();
            vkCmdBeginRenderPass(_command_buffer, &rp_begin_info, VK_SUBPASS_CONTENTS_INLINE);

            for (uint32_t stage_index = 0u;
                 stage_index < (uint32_t)node_info.sequence.size();
                 ++stage_index)
            {
                vkCmdBindPipeline(_command_buffer,
                                  VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  framebuffer.stages[stage_index]);

                for (uint32_t dispatch_index = 0u;
                     dispatch_index < node_info.sequence[stage_index].group_instance_count;
                     ++dispatch_index)
                {
                    for (uint32_t group_index = 0u;
                         group_index < (uint32_t)node_info.sequence[stage_index].binding_groups.size();
                         ++group_index)
                    {
                        VkDescriptorSet descset =
                            runtime.stages[stage_index].binding_groups[group_index].sets[dispatch_index];
                        vkCmdBindDescriptorSets(_command_buffer,
                                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                runtime.stages[stage_index].pipeline_layout,
                                                group_index,
                                                1u, &descset,
                                                0u, nullptr);
                    }

                    for (uint32_t range_index = 0;
                         range_index < (uint32_t)node_info.sequence[stage_index].push_constants.size();
                         ++range_index)
                    {
                        DispatchStageInfo::PushConstantRange const& range =
                            node_info.sequence[stage_index].push_constants[range_index];
                        void* memory = bindings
                            .stages[stage_index]
                            .instances[dispatch_index]
                            .push_constants[range_index].get();
                        vkCmdPushConstants(_command_buffer,
                                           runtime.stages[stage_index].pipeline_layout,
                                           range.stages,
                                           range.offset,
                                           range.size,
                                           memory);
                    }

                    GraphicsCommandBinding const& graphics_command =
                        bindings.stages[stage_index].instances[dispatch_index].graphics;

                    switch(graphics_command.command)
                    {
                    case GraphicsCommandBinding::DispatchCommand_Draw:
                    {
                        vkCmdDraw(_command_buffer,
                                  graphics_command.vertex_count,
                                  graphics_command.instance_count,
                                  0, 0);
                    } break;
                    default: break;
                    }
                }
            }

            vkCmdEndRenderPass(_command_buffer);
        }
    }

    void BindSwapchain(DispatchNodeInfo const* _node,
                       vktk::Swapchain const& _swapchain)
    {
        uint32_t node_index = (uint32_t)(_node - graph->nodes.data());
        if (nodes_bindings.size() < node_index+1)
            nodes_bindings.resize(node_index+1);

        DispatchNodeFramebuffer const& framebuffer = nodes_framebuffer[node_index];
        DispatchNodeBindings& node_bindings = nodes_bindings[node_index];

        for (uint32_t index = 0u; index < (uint32_t)_swapchain.framebuffers.size(); ++index)
        {
            node_bindings.framebuffers.push_back(
                vulkan->CreateFramebuffer(framebuffer.graphics.render_pass,
                                          { _swapchain.size.width, _swapchain.size.height },
                                          { _swapchain.views[index*2], _swapchain.views[index*2 + 1] })
                );
        }
    }

    void BindFramebufferAttachments(DispatchNodeInfo const* _node,
                                    numtk::vec2u const& _size,
                                    std::vector<VkImageView> const& _attachments)
    {
        uint32_t node_index = (uint32_t)(_node - graph->nodes.data());
        if (nodes_bindings.size() < node_index+1)
            nodes_bindings.resize(node_index+1);

        DispatchNodeFramebuffer const& framebuffer = nodes_framebuffer[node_index];
        DispatchNodeBindings& node_bindings = nodes_bindings[node_index];
        node_bindings.framebuffers.push_back(
            vulkan->CreateFramebuffer(framebuffer.graphics.render_pass,
                                      _size,
                                      _attachments));
    }

    void BindFramebufferArea(DispatchNodeInfo const* _node,
                             FramebufferAreaBinding const& _area_binding)
    {
        uint32_t node_index = (uint32_t)(_node - graph->nodes.data());
        if (nodes_bindings.size() < node_index+1)
            nodes_bindings.resize(node_index+1);

        DispatchNodeBindings& node_bindings = nodes_bindings[node_index];
        node_bindings.area_binding = _area_binding;
    }

    void BindGraphicsCommand(DispatchNodeInfo const* _node,
                             uint32_t _stage_index,
                             uint32_t _instance_index,
                             GraphicsCommandBinding const& _command)
    {
        uint32_t node_index = (uint32_t)(_node - graph->nodes.data());
        if (nodes_bindings.size() < node_index+1)
            nodes_bindings.resize(node_index+1);

        DispatchNodeBindings& node_bindings = nodes_bindings[node_index];
        if (node_bindings.stages.size() < _stage_index+1)
            node_bindings.stages.resize(_stage_index+1);
        if (node_bindings.stages[_stage_index].instances.size() < _instance_index+1)
            node_bindings.stages[_stage_index].instances.resize(_instance_index+1);

        GraphicsCommandBinding& graphics = node_bindings.stages[_stage_index].instances[_instance_index].graphics;
        graphics = _command;
    }

    void* BindPushConstants(NodeBindingPoint const& _location,
                            uint32_t _instance_index)
    {
        DispatchStageInfo const& stage = _location.node->sequence[_location.stage_index];
        DispatchStageInfo::PushConstantRange const& range = stage.push_constants[_location.push_constants.range_index];

        uint32_t node_index = (uint32_t)(_location.node - graph->nodes.data());
        if (nodes_bindings.size() < node_index+1)
            nodes_bindings.resize(node_index+1);

        DispatchNodeBindings& node_bindings = nodes_bindings[node_index];
        if (node_bindings.stages.size() < _location.stage_index+1)
            node_bindings.stages.resize(_location.stage_index+1);
        if (node_bindings.stages[_location.stage_index].instances.size() < _instance_index+1)
            node_bindings.stages[_location.stage_index].instances.resize(_instance_index+1);

        DispatchStageBindings& stage_bindings = node_bindings.stages[_location.stage_index];
        DispatchStageBindings::DispatchInstance& instance = stage_bindings.instances[_instance_index];

        if (instance.push_constants.size() < _location.push_constants.range_index+1)
            instance.push_constants.resize(_location.push_constants.range_index+1);
        instance.push_constants[_location.push_constants.range_index].reset(new uint8_t[range.size]);

        return instance.push_constants[_location.push_constants.range_index].get();
    }

    VkWriteDescriptorSet BindShaderResource(NodeBindingPoint const& _location,
                                            uint32_t _instance_index,
                                            ShaderResourceBinding const& _resources)
    {
        uint32_t node_index = (uint32_t)(_location.node - graph->nodes.data());
        DispatchNodeStatic const& static_runtime = nodes_static[node_index];
        DispatchStageStatic const& stage_static = static_runtime.stages[_location.stage_index];
        DispatchStageStatic::BindingGroup const& group_runtime = stage_static.binding_groups[_location.shader.group_index];

        DispatchStageInfo const& stage = _location.node->sequence[_location.stage_index];
        DispatchStageInfo::BindingGroupInfo const& binding_group = stage.binding_groups[_location.shader.group_index];
        DispatchStageInfo::BindingInfo const& binding = binding_group.bindings[_location.shader.binding_index];

        VkWriteDescriptorSet desc_write = {};
        desc_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        desc_write.pNext = nullptr;
        desc_write.dstSet = group_runtime.sets[_instance_index];
        desc_write.dstBinding = _location.shader.binding_index;
        desc_write.dstArrayElement = 0u;
        desc_write.descriptorCount = _resources.descriptor_count;
        desc_write.descriptorType = binding.type;
        desc_write.pImageInfo = _resources.images;
        desc_write.pBufferInfo = _resources.buffers;
        desc_write.pTexelBufferView = nullptr;

        return desc_write;
    }
};

struct PNextEntry {
    uint32_t binding_group_index;
    void* pnext;
};
using PNextMap = std::unordered_map<std::string, std::vector<PNextEntry>>;

DispatchNodeStatic GenerateDispatchStaticRuntime(DispatchNodeInfo const& _node,
                                                 vktk::Context* _vulkan,
                                                 PNextMap const& _pNext_map = {})
{
    DispatchNodeStatic static_runtime = {};

    for (DispatchStageInfo const& stage : _node.sequence)
    {
        DispatchStageStatic stage_static = {};
        std::vector<VkDescriptorSetLayout> set_layouts = {};
        std::vector<VkPushConstantRange> push_constant_ranges = {};

#if 0
        void* stage_pnext = nullptr;
        if (_pNext_map.count(stage.name))
            stage_pnext = _pNext_map[stage.name];
#endif
        std::vector<PNextEntry> stage_pnexts = {};
        if (_pNext_map.count(stage.name))
            stage_pnexts = _pNext_map.at(stage.name);

        //for (DispatchStageInfo::BindingGroupInfo const& binding_group : stage.binding_groups)
        for (uint32_t group_index = 0u; group_index < (uint32_t)stage.binding_groups.size(); ++group_index)
        {
            DispatchStageInfo::BindingGroupInfo const& binding_group = stage.binding_groups[group_index];

            DispatchStageStatic::BindingGroup descriptor = {};
            std::vector<vktk::Context::DescriptorSetBindingPoint> bindings = {};

            for (DispatchStageInfo::BindingInfo const& binding : binding_group.bindings)
            {
                bindings.push_back(vktk::Context::DescriptorSetBindingPoint{
                    binding.type,
                    binding.flags,
                    binding.stages,
                    binding.count
                });
            }

            descriptor.pool = _vulkan->CreateDescriptorPool(stage.group_instance_count, bindings);
            descriptor.sets.resize(stage.group_instance_count);

            void* pnext_begin = nullptr;

            VkDescriptorSetVariableDescriptorCountAllocateInfo desc_count_alloc;
#if 0
            if (std::find_if(binding_group.bindings.begin(),
                             binding_group.bindings.end(),
                             [](DispatchStageInfo::BindingInfo const& binding) {
                                 return binding.flags & VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;
                             })
                != binding_group.bindings.end())
            {
                void* desc_count_alloc_ptr = vktk::PNextLookup(
                    (VkBaseOutStructure*)stage_pnext,
                    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO);
                desc_count_alloc = desc_count_alloc_ptr
                    ? *(VkDescriptorSetVariableDescriptorCountAllocateInfo const*)desc_count_alloc_ptr
                    : VkDescriptorSetVariableDescriptorCountAllocateInfo{};
                desc_count_alloc.pNext = nullptr;
                pnext_begin = desc_count_alloc_ptr ? &desc_count_alloc : nullptr;
            }
#endif

            {
                auto group_pnext_it = std::find_if(
                    stage_pnexts.begin(), stage_pnexts.end(),
                    [group_index](PNextEntry const& entry) {
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

            VkDescriptorSetAllocateInfo descset_allocate{};
            descset_allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            descset_allocate.pNext = pnext_begin;
            descset_allocate.descriptorPool = descriptor.pool.pool;
            descset_allocate.descriptorSetCount = 1u;
            descset_allocate.pSetLayouts = &descriptor.pool.layout;
            for (uint32_t index = 0; index < stage.group_instance_count; ++index)
                CHECKCALL(vkAllocateDescriptorSets, _vulkan->device,
                          &descset_allocate, &descriptor.sets[index]);

            stage_static.binding_groups.push_back(descriptor);

            set_layouts.push_back(descriptor.pool.layout);
        }

        for (DispatchStageInfo::PushConstantRange const& push_constant : stage.push_constants)
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
        CHECKCALL(vkCreatePipelineLayout, _vulkan->device, &pipeline_create, nullptr,
                  &stage_static.pipeline_layout);

        static_runtime.stages.push_back(stage_static);
    }

    return static_runtime;
}

struct voxel_state_t
{
    float time = 0.f;

    //TurntableCamera camera;
    FlightCamera camera;

    numtk::vec2f sun_spherical = { -2.f, 0.25f };

    uint32_t frame_index = 0;
    bool pixel_cycling_enabled = false;

    vktk::Texture primary_pass_color;
    vktk::Texture primary_pass_depth;

    DispatchGraph forward_graph;
    DispatchRuntime forward_runtime;
    DispatchGraph pixel_cycling_graph;
    DispatchRuntime pixel_cycling_runtime;

    DispatchGraph sparse_graph;
    DispatchRuntime sparse_runtime;

    DispatchRuntime* current_runtime;

    VkFence last_submit;

    vktk::Buffer matrix_buffer;
    vktk::Buffer staging_buffer;
    void* staging_area;

    VkSampler sampler;
    vktk::Texture region_texture;
    RegionData region;

    std::vector<uint16_t> section_map{};
    uint32_t section_index = 0;
    std::vector<vktk::Texture> section_textures{};

    voxtk::Region<uint8_t> voxel_region{ { 512u, 512u, 512u }, (uint8_t)0 };
    voxtk::Region<uint8_t> tilemap_region{ { 512u, 512u, 512u, }, (uint8_t)0 };
    vktk::Texture tilemap;
    vktk::Texture sparse_texture;
    std::vector<vktk::Texture> masks{};
    std::vector<vktk::Texture> offsets{};
    vktk::Buffer nodes_buffer;
};

struct MatrixBlock
{
    numtk::mat4f projection;
    numtk::mat4f view_model;
    numtk::mat4f view;
    numtk::vec2f screen_offset;
    numtk::vec2i screen_size;
    float padding[12];
};

struct SkyboxPushConstants
{
    numtk::mat4f projection;
    numtk::mat4f view;
    numtk::vec4f position;
    numtk::vec3f light_dir;
};

DispatchStageInfo RegionPassDispatchStageInfo()
{
    DispatchStageInfo output{};
    output.name = "render_region";

    output.shaders = std::vector<DispatchStageInfo::ShaderStage>{
        { VK_SHADER_STAGE_VERTEX_BIT,
          { "#version 450",
            "#define CW_CUBE",
            #include "shaders/cube_vertices.vert.h"
            ,
            #include "shaders/draw_cube.vert.h"
          }
        },
        { VK_SHADER_STAGE_FRAGMENT_BIT,
          { "#version 450",
            #include "shaders/voxel_traversal.h"
            ,
            #include "shaders/render_region.frag.h"
          }
        }
    };

    output.group_instance_count = 64;
    output.binding_groups.push_back(DispatchStageInfo::BindingGroupInfo{
        {
            { VK_SHADER_STAGE_FRAGMENT_BIT, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
              0, 1 },
            { VK_SHADER_STAGE_ALL, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
              0, 1 },
            { VK_SHADER_STAGE_FRAGMENT_BIT, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
              VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT, 32*32*32 },
        }
    });

    output.push_constants = std::vector<DispatchStageInfo::PushConstantRange>{
        { VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(numtk::vec3f) }
    };

    output.graphics.state_overrides = std::vector<vktk::Context::StateOverride>{};
    output.graphics.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    output.graphics.input_layout = std::vector<vktk::Context::VertexInput>{};

    return output;
}

DispatchStageInfo SparseRegionDispatchStageInfo()
{
    DispatchStageInfo output = {};
    output.name = "sparse_region";

    output.shaders = std::vector<DispatchStageInfo::ShaderStage>{
        { VK_SHADER_STAGE_VERTEX_BIT,
          { "#version 450",
            "#define CW_CUBE",
            #include "shaders/cube_vertices.vert.h"
            ,
            #include "shaders/draw_cube.vert.h"
          }
        },
        { VK_SHADER_STAGE_FRAGMENT_BIT,
          { "#version 450",
            #include "shaders/voxel_traversal.h"
            ,
            #include "shaders/sparse_region.frag.h"
          }
        }
    };

    output.group_instance_count = 1;

    output.binding_groups.push_back(DispatchStageInfo::BindingGroupInfo{
        {
            { VK_SHADER_STAGE_FRAGMENT_BIT, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
              0, 1 },
            { VK_SHADER_STAGE_ALL, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
              0, 1 },
        }
    });

    output.binding_groups.push_back(DispatchStageInfo::BindingGroupInfo{
        {
            { VK_SHADER_STAGE_FRAGMENT_BIT, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
              VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT, 512*512 },
        }
    });

#if 1
    output.binding_groups.push_back(DispatchStageInfo::BindingGroupInfo{
        {
            { VK_SHADER_STAGE_FRAGMENT_BIT, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
              VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT, 512*512 },
        }
    });
#endif

    output.push_constants = std::vector<DispatchStageInfo::PushConstantRange>{
    };

    output.graphics.state_overrides = std::vector<vktk::Context::StateOverride>{};
    output.graphics.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    output.graphics.input_layout = std::vector<vktk::Context::VertexInput>{};

    return output;
}

DispatchStageInfo SkyboxDispatchStageInfo()
{
    DispatchStageInfo output = {};
    output.name = "skybox";

    output.shaders = std::vector<DispatchStageInfo::ShaderStage>{
        { VK_SHADER_STAGE_VERTEX_BIT,
          { "#version 450",
            #include "shaders/full_screen.vert.h"
          }
        },
        { VK_SHADER_STAGE_FRAGMENT_BIT,
          { "#version 450",
            #include "shaders/skybox.frag.h"
          }
        }
    };

    output.group_instance_count = 1;
    output.binding_groups = std::vector<DispatchStageInfo::BindingGroupInfo>{};
    output.push_constants = std::vector<DispatchStageInfo::PushConstantRange>{
        { VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(SkyboxPushConstants) }
    };

    output.graphics.state_overrides = std::vector<vktk::Context::StateOverride>{};
    output.graphics.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    output.graphics.input_layout = std::vector<vktk::Context::VertexInput>{};

    return output;
}

DispatchStageInfo ResolveDispatchStageInfo()
{
    DispatchStageInfo output = {};
    output.name = "resolve";

    output.shaders = std::vector<DispatchStageInfo::ShaderStage>{
        { VK_SHADER_STAGE_VERTEX_BIT,
          { "#version 450",
            #include "shaders/full_screen.vert.h"
          }
        },
        { VK_SHADER_STAGE_FRAGMENT_BIT,
          { "#version 450",
            #include "shaders/resolve.frag.h"
          }
        }
    };

    output.group_instance_count = 1;
    output.binding_groups.push_back(DispatchStageInfo::BindingGroupInfo{
        {
            { VK_SHADER_STAGE_FRAGMENT_BIT, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
              0, 1 }
        }
    });

    output.push_constants = std::vector<DispatchStageInfo::PushConstantRange>{
        { VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(numtk::vec2i) }
    };

    output.graphics.state_overrides = std::vector<vktk::Context::StateOverride>{};
    output.graphics.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    output.graphics.input_layout = std::vector<vktk::Context::VertexInput>{};

    return output;
}

DispatchNodeInfo MainWorldDispatchInfo()
{
    DispatchNodeInfo output{};
    output.name = "MainWorldDispatch";
    output.mode = DispatchNodeInfo::DispatchMode_Graphics;
    output.sequence = std::vector<DispatchStageInfo>{
        RegionPassDispatchStageInfo(),
        SkyboxDispatchStageInfo()
    };

    output.graphics.attachments = std::vector<DispatchNodeInfo::RenderAttachment>{
        {
            VK_IMAGE_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM,
            numtk::vec4f{ 0.f, 1.f, 1.f, 0.f }
        },
        {
            VK_IMAGE_TYPE_2D, VK_FORMAT_D32_SFLOAT,
            DispatchNodeInfo::ClearValue{ 1.f, 0u }
        }
    };

    return output;
}

DispatchNodeInfo ResolveDispatchInfo()
{
    DispatchNodeInfo output{};
    output.name = "ResolveDispatch";
    output.mode = DispatchNodeInfo::DispatchMode_Graphics;
    output.sequence = std::vector<DispatchStageInfo>{
        ResolveDispatchStageInfo()
    };

    output.graphics.attachments = std::vector<DispatchNodeInfo::RenderAttachment>{
        {
            VK_IMAGE_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM,
            numtk::vec4f{ 0.f, 1.f, 1.f, 0.f }
        },
        {
            VK_IMAGE_TYPE_2D, VK_FORMAT_D32_SFLOAT,
            DispatchNodeInfo::ClearValue{ 1.f, 0u }
        }
    };

    return output;
}

DispatchGraph FullFrameRenderGraph()
{
    DispatchGraph output = {};
    output.nodes.push_back(MainWorldDispatchInfo());

    output.InsertEdge(GraphEdge{
        GraphEdge::EdgeType_Image,
        NodeBindingPoint{
            .node = &output.nodes[0],
            .stage_index = (uint32_t)-1,
            .type = NodeBindingPoint::BindingType_Framebuffer,
            .framebuffer = { 0 }
        },
        NodeBindingPoint{
            .node = nullptr,
            .stage_index = (uint32_t)-1,
            .type = NodeBindingPoint::BindingType_Present
        }
    });

    return output;
}

DispatchGraph SparseRegionRenderGraph()
{
    DispatchNodeInfo node{};
    node.name = "SparseRegionDispatch";
    node.mode = DispatchNodeInfo::DispatchMode_Graphics;
    node.sequence = std::vector<DispatchStageInfo>{
        SparseRegionDispatchStageInfo(),
    };

    node.graphics.attachments = std::vector<DispatchNodeInfo::RenderAttachment>{
        {
            VK_IMAGE_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM,
            numtk::vec4f{ 0.f, 1.f, 1.f, 0.f }
        },
        {
            VK_IMAGE_TYPE_2D, VK_FORMAT_D32_SFLOAT,
            DispatchNodeInfo::ClearValue{ 1.f, 0u }
        }
    };

    DispatchGraph output = {};
    output.nodes.push_back(node);

    output.InsertEdge(GraphEdge{
        GraphEdge::EdgeType_Image,
        NodeBindingPoint{
            .node = &output.nodes[0],
            .stage_index = (uint32_t)-1,
            .type = NodeBindingPoint::BindingType_Framebuffer,
            .framebuffer = { 0 }
        },
        NodeBindingPoint{
            .node = nullptr,
            .stage_index = (uint32_t)-1,
            .type = NodeBindingPoint::BindingType_Present
        }
    });

    return output;
}

DispatchGraph PixelCyclingRenderGraph()
{
    DispatchGraph output = {};
    output.nodes.push_back(MainWorldDispatchInfo());
    output.nodes.push_back(ResolveDispatchInfo());

    output.InsertEdge(GraphEdge{
        GraphEdge::EdgeType_Image,
        NodeBindingPoint{
            .node = &output.nodes[0],
            .stage_index = (uint32_t)-1,
            .type = NodeBindingPoint::BindingType_Framebuffer,
            .framebuffer = { 0 }
        },
        NodeBindingPoint{
            .node = &output.nodes[1],
            .stage_index = 0,
            .type = NodeBindingPoint::BindingType_Shader,
            .shader = { 0, 0 }
        }
    });

    output.InsertEdge(GraphEdge{
        GraphEdge::EdgeType_Image,
        NodeBindingPoint{
            .node = &output.nodes[1],
            .stage_index = 0,
            .type = NodeBindingPoint::BindingType_Shader,
            .shader = { 0, 0 }
        },
        NodeBindingPoint{
            .node = &output.nodes[0],
            .stage_index = (uint32_t)-1,
            .type = NodeBindingPoint::BindingType_Framebuffer,
            .framebuffer = { 0 }
        }
    });

    output.InsertEdge(GraphEdge{
        GraphEdge::EdgeType_Image,
        NodeBindingPoint{
            .node = &output.nodes[1],
            .stage_index = (uint32_t)-1,
            .type = NodeBindingPoint::BindingType_Framebuffer,
            .framebuffer = { 0 }
        },
        NodeBindingPoint{
            .node = nullptr,
            .stage_index = (uint32_t)-1,
            .type = NodeBindingPoint::BindingType_Present
        }
    });

    return output;
}

void DecodeChunk(voxtk::Region<uint8_t>& region,
                 mctk::MCAChunkData const& chunk_data,
                 numtk::vec2u const& chunk_xz)
{
    using namespace mctk;

    std::vector<NBTField> chunk_fields = MCAPackedNBTDecode(chunk_data);
    if (chunk_fields.empty())
        return;

    NBTCompound const& root = *(NBTCompound const*)chunk_fields[0].payload;
    for (NBTField const& field0 : root)
    {
        if (field0.name == "sections"
            && field0.type == TAG_List)
        {
            NBTList const& sections = *(NBTList const*)field0.payload;
            NBTCompound const* compounds = (NBTCompound const*)sections.data;
            for (uint32_t index = 0u; index < sections.size; ++index)
            {
                NBTCompound const& compound = compounds[index];
                //int32_t section_index = -1;
                //RegionData::SectionData section_data = {};
                NBTCompound const* block_states = nullptr;
                int8_t section_y = -1;

                for (NBTField const& field1 : compound)
                {
                    if (field1.name == "Y"
                        && field1.type == TAG_Byte)
                        section_y = (*(int8_t const*)field1.payload) + 4;

                    if (field1.name == "block_states"
                        && field1.type == TAG_Compound)
                    {
                        block_states = (NBTCompound const*)field1.payload;
                    }
                }

                if (section_y >= 0 && block_states)
                {
                    numtk::vec3u section_begin =
                        numtk::vec3u{ chunk_xz.x(), (uint32_t)section_y, chunk_xz.y() } * 16u;
                    numtk::vec3u section_end =
                        section_begin + numtk::vec3u::Constant(16u);

                    //NBTCompound const& block_states = *(NBTCompound const*)field1.payload;
                    NBTLongArray const* block_data_field = nullptr;
                    uint32_t palette_size = 0u;
                    std::vector<std::string> palette_data = {};

                    for (NBTField const& field2 : *block_states)
                    {
                        if (field2.name == "palette"
                            && field2.type == TAG_List)
                        {
                            NBTList const& palette = *((NBTList const*)field2.payload);
                            NBTCompound const* blocks = (NBTCompound const*)palette.data;
                            palette_size = palette.size;

                            for (uint32_t block_index = 0u;
                                 block_index < palette_size;
                                 ++block_index)
                            {
                                NBTCompound const& block = blocks[block_index];

                                for (NBTField const& field3 : block)
                                    if (field3.name == "Name"
                                        && field3.type == TAG_String)
                                        palette_data.push_back(
                                            (*(NBTString const*)field3.payload).substr(10));
                            }
                        }

                        if (field2.name == "data"
                            && field2.type == TAG_Long_Array)
                            block_data_field = (NBTLongArray const*)field2.payload;
                    }

                    if (palette_size == 1)
                    {
                        region.Set(section_begin, section_end, (uint8_t)0);
                        continue;
                    }

                    NBTLongArray const& block_data = *block_data_field;
                    uint32_t bit_count = numtk::log2(palette_size-1);
                    if (palette_size - (1 << (bit_count-1)))
                        ++bit_count;
                    bit_count = std::max(bit_count, 4u);

                    uint64_t block_index = 0;
                    uint32_t data_index = 0;
                    uint8_t data_offset = 0;
                    while (block_index < 4096 && data_index < block_data.size())
                    {
                        if (data_offset + bit_count > 64)
                        {
                            data_offset = 0;
                            ++data_index;
                        }

                        uint16_t block_id = (uint16_t)
                            ((block_data[data_index] >> data_offset)
                             & ((1ull << bit_count) - 1));

                        uint32_t x = block_index % 16;
                        uint32_t z = (block_index / 16) % 16;
                        uint32_t y = (block_index / 256) % 16;

                        numtk::vec3u point = section_begin + numtk::vec3u{ x, y, z };
                        if (palette_data[block_id] != "air"
                            && palette_data[block_id] != "glow_lichen")
                            region.Set(point, (uint8_t)block_id);

                        data_offset += bit_count;
                        ++block_index;
                    }
                }
            }
        }
    }
}

voxtk::Region<uint8_t> DeserialiseRegion(std::string const& file_path)
{
    systk::Timer scope_time{ "DeserialiseRegion",
        [](std::string const& tag, float time) {
            std::cout << "[" << tag << "] " << time << " s" << std::endl;
        }
    };

    std::vector<uint8_t> region_file = systk::LoadFile(file_path.c_str());
    mctk::MCAFileData mca_data = mctk::MCADecode(region_file.data(), region_file.size());
    voxtk::Region<uint8_t> output{ numtk::vec3u{ 512u, 512u, 512u }, (uint8_t)0 };

    for (uint32_t index = 0; index < 1024; ++index)
    {
        uint32_t chunk_x = index % 32;
        uint32_t chunk_z = index / 32;
        DecodeChunk(output, mca_data.chunks[index], numtk::vec2u{ chunk_x, chunk_z });
    }

    return output;
}

void LoadSection(voxel_state_t* state, vktk::Context* vulkan, vktk::CommandBuffer& update_commands,
                 numtk::vec3<int32_t> const& coords)
{
    std::vector<std::unique_ptr<uint8_t[]>> mip_levels = {};

    voxtk::Region<uint8_t> const& region = state->voxel_region;
    uint16_t section_count = state->section_map[coords[0] + coords[1]*32 + coords[2]*32*32];
    if (section_count != 0u && section_count != 4096u)
    {
        state->section_textures.push_back(
            vulkan->CreateTexture(16, 16, 16, 5,
                                  VK_IMAGE_TYPE_3D, VK_FORMAT_R8_UNORM,
                                  VK_IMAGE_USAGE_TRANSFER_DST_BIT
                                  | VK_IMAGE_USAGE_SAMPLED_BIT));

        update_commands.ImageTransitions(
            VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            {
                {
                    state->section_textures.back().image,
                    VK_ACCESS_NONE, VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_IMAGE_ASPECT_COLOR_BIT,
                }
            }
        );

        mip_levels.emplace_back(new uint8_t[4096]);
        {
            uint8_t* blocks = mip_levels.back().get();
            for (uint32_t pointz = 0; pointz < 16; ++pointz)
            {
                for (uint32_t pointy = 0; pointy < 16; ++pointy)
                {
                    for (uint32_t pointx = 0; pointx < 16; ++pointx)
                    {
                        uint32_t index = pointx + pointy*16u + pointz*256u;
                        numtk::vec3u point {
                            (uint32_t)coords.x * 16u + pointx,
                            (uint32_t)coords.y * 16u + pointy,
                            (uint32_t)coords.z * 16u + pointz
                        };
                        blocks[index] = (region.Test(point) ? 255u : 0u);
                    }
                }
            }

            update_commands.UpdateTexture(
                state->section_textures.back(), { 0, 0, 0 }, { 16, 16, 16 }, 0,
                [blocks](void* mapped_data) {
                    std::memcpy(mapped_data, blocks, 4096);
                }
            );
        }

        for (uint32_t mip_index = 0; mip_index < 4; ++mip_index)
        {
            uint32_t src_mip_size = 16 >> mip_index;
            uint32_t dst_mip_size = 8 >> mip_index;
            uint32_t block_count = 4096 / (8 << (3*mip_index));
            mip_levels.emplace_back(new uint8_t[block_count]);

            uint8_t* src = mip_levels[mip_index].get();
            uint8_t* dst = mip_levels[mip_index+1].get();
            for (uint32_t index = 0u; index < block_count; ++index)
            {
                uint32_t x = index % dst_mip_size;
                uint32_t y = (index / dst_mip_size) % dst_mip_size;
                uint32_t z = (index / (dst_mip_size*dst_mip_size));

                uint8_t v = 0;
                //uint8_t v = 255;
                for (uint32_t sample = 0u; sample < 8; ++sample)
                {
                    uint32_t sample_x = x*2 + (sample&1);
                    uint32_t sample_y = y*2 + ((sample>>1)&1);
                    uint32_t sample_z = z*2 + ((sample>>2)&1);
                    uint32_t input_index =
                        sample_x
                        + sample_y * src_mip_size
                        + sample_z * src_mip_size * src_mip_size;

                    uint8_t block = src[input_index];
                    v = std::max(v, block);
                    //v = std::min(v, block);
                }
                dst[index] = v;
            }

            update_commands.UpdateTexture(
                state->section_textures.back(),
                { 0, 0, 0 }, { dst_mip_size, dst_mip_size, dst_mip_size },
                mip_index + 1,
                [dst, block_count](void* mapped_data) {
                    std::memcpy(mapped_data, dst, block_count);
                }
            );
        }

        update_commands.ImageTransitions(
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            {
                {
                    state->section_textures.back().image,
                    VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_IMAGE_ASPECT_COLOR_BIT,
                }
            }
        );
    }

    update_commands.ImageTransitions(
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        {
            {
                state->region_texture.image,
                VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_ASPECT_COLOR_BIT,
            }
        });

    if (state->section_textures.size() > state->section_index)
    {
        update_commands.UpdateTexture(
            state->region_texture,
            VkOffset3D{ coords[0], coords[1], coords[2] },
            VkExtent3D{ 1, 1, 1 },
            [section_index = state->section_index,
             &mip_levels](void* mapped_data) {
                uint16_t* section_info = (uint16_t*)mapped_data;
                section_info[1] = (uint16_t)section_index;
            });

        state->section_index++;
    }
    else
    {
        update_commands.UpdateTexture(
            state->region_texture,
            VkOffset3D{ coords[0], coords[1], coords[2] },
            VkExtent3D{ 1, 1, 1 },
            [section_count](void* mapped_data) {
                uint16_t* section_info = (uint16_t*)mapped_data;
                if (section_count == 0)
                    section_info[1] = 0x8000;
                else
                    section_info[1] = 0x80ff;
            });
    }

    update_commands.ImageTransitions(
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        {
            {
                state->region_texture.image,
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_IMAGE_ASPECT_COLOR_BIT,
            }
        });
}

void LoadArea(voxel_state_t* _state, vktk::Context* _vulkan,
              numtk::vec3<int32_t> const& _begin,
              numtk::vec3<int32_t> const& _end)
{
    systk::Timer scope_time{ "LoadArea",
        [](std::string const& tag, float time) {
            std::cout << "[" << tag << "] " << time << " s" << std::endl;
        }
    };

    vktk::CommandBuffer update_commands = _vulkan->CreateCommandBuffer(_vulkan->present_queue);
    update_commands.Begin();

    uint32_t sync_counter = 0;
    for (int32_t x = _begin.x; x < _end.x; ++x)
    {
        for (int32_t y = _begin.y; y < _end.y; ++y)
        {
            for (int32_t z = _begin.z; z < _end.z; ++z)
            {
                LoadSection(_state, _vulkan, update_commands, { x, y, z });
            }
        }
    }

    update_commands.End();
    update_commands.Submit();
}

void LoadRegion(voxel_state_t* state, vktk::Context* vulkan)
{
    systk::Timer scope_time{ "LoadRegion",
        [](std::string const& tag, float time) {
            std::cout << "[" << tag << "] " << time << " s" << std::endl;
        }
    };

    voxtk::Region<uint8_t> const& region = state->voxel_region;

    state->section_map.resize(32*32*32);

    for (uint32_t zindex = 0; zindex < 32; ++zindex)
    {
        for (uint32_t yindex = 0; yindex < 32; ++yindex)
        {
            for (uint32_t xindex = 0; xindex < 32; ++xindex)
            {
                uint32_t index = xindex + yindex*32 + zindex*32*32;
                state->section_map[index] = 0;
                if (yindex < 24)
                {
                    uint16_t count = 0;

                    for (uint32_t pointz = 0; pointz < 16; ++pointz)
                    {
                        for (uint32_t pointy = 0; pointy < 16; ++pointy)
                        {
                            for (uint32_t pointx = 0; pointx < 16; ++pointx)
                            {
                                numtk::vec3u point{
                                    xindex * 16u + pointx,
                                    yindex * 16u + pointy,
                                    zindex * 16u + pointz
                                };
                                if (region.Test(point))
                                    ++count;
                            }
                        }
                    }

                    state->section_map[index] = count;
                }
            }
        }
    }

    state->region_texture = vulkan->CreateTexture(
        32, 32, 32,
        VK_IMAGE_TYPE_3D, VK_FORMAT_R16G16_UINT,
        VK_IMAGE_USAGE_SAMPLED_BIT,
        [state](void* mapped_data) {
            uint16_t* sections = (uint16_t*)mapped_data;

            for (uint32_t zindex = 0; zindex < 32; ++zindex)
            {
                for (uint32_t yindex = 0; yindex < 32; ++yindex)
                {
                    for (uint32_t xindex = 0; xindex < 32; ++xindex)
                    {
                        uint32_t index = xindex + yindex*32 + zindex*32*32;
                        sections[index * 2+1] = 0xffff;
                        sections[index * 2] = (uint16_t)(((float)state->section_map[index] / 4096.f) * 65535.f);
                    }
                }
            }
        });
}

void LoadRegionSparse(voxel_state_t* state, vktk::Context* vulkan)
{
#define AREA_SIZE 128u
#define NODE_SIZE 16u
#define SCALE_RATIO AREA_SIZE/NODE_SIZE

#if 1
    std::vector<voxtk::Region<uint8_t>> lods = {};

    voxtk::Region<uint8_t>* src = &state->voxel_region;
    for (uint32_t lod_size = AREA_SIZE/NODE_SIZE, src_index = 0u;
         lod_size > 1;
         (lod_size /= NODE_SIZE*4, ++src_index))
    {
        lods.push_back(voxtk::Region<uint8_t>{ { lod_size, lod_size, lod_size }, (uint8_t)0 });

        if (src_index)
            src = &lods[src_index-1];
        voxtk::Region<uint8_t>& dst = lods.back();

        for (uint32_t dstz = 0; dstz < dst.size.z; ++dstz)
        {
            for (uint32_t dsty = 0; dsty < dst.size.y; ++dsty)
            {
                for (uint32_t dstx = 0; dstx < dst.size.x; ++dstx)
                {
                    numtk::vec3u dstbase{ dstx, dsty, dstz };

                    bool bit = [] (voxtk::Region<uint8_t> const& src,
                                   numtk::vec3u const& dstbase) {
                        for (uint32_t srcz = 0; srcz < NODE_SIZE; ++srcz)
                        {
                            for (uint32_t srcy = 0; srcy < NODE_SIZE; ++srcy)
                            {
                                for (uint32_t srcx = 0; srcx < NODE_SIZE; ++srcx)
                                {
                                    numtk::vec3u srcbase = dstbase*NODE_SIZE*4
                                        + numtk::vec3u{ srcx, srcy, srcz }*4;
                                    uint64_t srcbits = src.ExtractKernel(srcbase);
                                    if (srcbits != 0)
                                        return true;
                                }
                            }
                        }
                        return false;
                    }(*src, dstbase);

                    dst.Set(dstbase, bit);
                }
            }
        }

        int i = 0;
    }

    uint32_t current_offset = 1u;
    for (uint32_t level_index = 0u; level_index < lods.size(); ++level_index)
    {
        voxtk::Region<uint8_t> const& lod = lods[lods.size()-1 - level_index];
        numtk::vec3u nextlod_size = numtk::max(lod.size / (NODE_SIZE * 4),
                                               numtk::vec3u::Constant(1));
#if 0
        numtk::vec3u nextlod_pixelsize = numtk::max(nextlod_size / 4,
                                                    numtk::vec3u::Constant(1));
#endif
        uint32_t nextlod_count = nextlod_size.x*nextlod_size.y*nextlod_size.z;

        for (uint32_t index = 0u; index < nextlod_count; ++index)
        {
            std::vector<uint32_t> level_offsets = { current_offset };

#if 0
            numtk::vec3u const nextlod_pixel_base {
                (index % nextlod_pixelsize.x),
                ((index / nextlod_pixelsize.x) % nextlod_pixelsize.y),
                (index / nextlod_pixelsize.x*nextlod_pixelsize.y)
            };
#endif

#if 1
            numtk::vec3u const nextlod_base {
                (index % nextlod_size.x),
                ((index / nextlod_size.x) % nextlod_size.y),
                (index / (nextlod_size.x*nextlod_size.y))
            };
#endif

            if (level_index != 0u && !lods[lods.size() - level_index].Test(nextlod_base))
                continue;

            numtk::vec3u const lod_base = nextlod_base * NODE_SIZE*4;

            numtk::vec3u const valid_range{
                std::min(NODE_SIZE, std::max((lod.size.x-lod_base.x)/4u, 1u)),
                std::min(NODE_SIZE, std::max((lod.size.y-lod_base.y)/4u, 1u)),
                std::min(NODE_SIZE, std::max((lod.size.z-lod_base.z)/4u, 1u))
            };

            state->masks.push_back(
                vulkan->CreateTexture(
                    NODE_SIZE, NODE_SIZE, NODE_SIZE,
                    VK_IMAGE_TYPE_3D, VK_FORMAT_R32G32_UINT,
                    VK_IMAGE_USAGE_SAMPLED_BIT,
                    [&lod, &lod_base, &level_offsets, &valid_range](void* mapped_data) {
                        uint64_t* output = (uint64_t*)mapped_data;
                        for (uint32_t zindex = 0; zindex < valid_range.z; ++zindex)
                        {
                            for (uint32_t yindex = 0; yindex < valid_range.y; ++yindex)
                            {
                                for (uint32_t xindex = 0; xindex < valid_range.x; ++xindex)
                                {
                                    numtk::vec3u base = numtk::vec3u{
                                        xindex * 4,
                                        yindex * 4,
                                        zindex * 4
                                    } + lod_base;

                                    uint64_t bits = lod.ExtractKernel(base);
                                    output[xindex + yindex*NODE_SIZE + zindex*NODE_SIZE*NODE_SIZE] = bits;
                                    level_offsets.push_back(level_offsets.back() + std::popcount(bits));
                                }

                                if (valid_range.x != NODE_SIZE)
                                    level_offsets.resize(level_offsets.size()
                                                         + (NODE_SIZE-valid_range.x),
                                                         level_offsets.back());
                            }

                            if (valid_range.y != NODE_SIZE)
                                level_offsets.resize(level_offsets.size()
                                                     + (NODE_SIZE-valid_range.y)*NODE_SIZE,
                                                     level_offsets.back());
                        }

                        if (valid_range.z != NODE_SIZE)
                            level_offsets.resize(level_offsets.size()
                                                 + (NODE_SIZE-valid_range.z)*NODE_SIZE*NODE_SIZE,
                                                 level_offsets.back());

                        int i = 0;
                    }
                )
            );

            state->offsets.push_back(
                vulkan->CreateTexture(
                    NODE_SIZE, NODE_SIZE, NODE_SIZE,
                    VK_IMAGE_TYPE_3D, VK_FORMAT_R32_UINT,
                    VK_IMAGE_USAGE_SAMPLED_BIT,
                    [&level_offsets, &valid_range](void* mapped_data) {
                        uint32_t* output = (uint32_t*)mapped_data;
                        std::memcpy(output, level_offsets.data(), NODE_SIZE*NODE_SIZE*NODE_SIZE*sizeof(uint32_t));
                    }
                )
            );

            current_offset = level_offsets.back();
        }
    }

    uint32_t level1_size = 512u / (NODE_SIZE * 4u);
    uint32_t level1_pixelsize = level1_size / 4u;
    uint32_t level1_count = level1_size * level1_size * level1_size;
    for (uint32_t index = 0u; index < level1_count; ++index)
    {
        numtk::vec3u const lod1_pixelbase {
            (index % level1_pixelsize),
            ((index / level1_pixelsize) % level1_pixelsize),
            (index / level1_pixelsize*level1_pixelsize)
        };

        numtk::vec3u const lod1_base{
            (index % level1_size),
            ((index / level1_size) % level1_size),
            (index / (level1_size*level1_size))
        };

        if (!lods[0].Test(lod1_base))
            continue;

        numtk::vec3u const lod0_base = lod1_base * NODE_SIZE*4u;

        state->masks.push_back(
            vulkan->CreateTexture(
                NODE_SIZE, NODE_SIZE, NODE_SIZE,
                VK_IMAGE_TYPE_3D, VK_FORMAT_R32G32_UINT,
                VK_IMAGE_USAGE_SAMPLED_BIT,
                [state, &lod0_base](void* mapped_data) {
                    uint64_t* output = (uint64_t*)mapped_data;
                    for (uint32_t zindex = 0; zindex < NODE_SIZE; ++zindex)
                    {
                        for (uint32_t yindex = 0; yindex < NODE_SIZE; ++yindex)
                        {
                            for (uint32_t xindex = 0; xindex < NODE_SIZE; ++xindex)
                            {
                                numtk::vec3u base = numtk::vec3u{
                                    xindex * 4,
                                    yindex * 4,
                                    zindex * 4
                                } + lod0_base;

                                uint64_t bits = state->voxel_region.ExtractKernel(base);
                                output[xindex + yindex*NODE_SIZE + zindex*NODE_SIZE*NODE_SIZE] = bits;
                            }
                        }
                    }
                }
            )
        );
    }

#endif

    state->sparse_texture = vulkan->CreateTexture(
        NODE_SIZE, NODE_SIZE, NODE_SIZE,
        VK_IMAGE_TYPE_3D, VK_FORMAT_R32G32_UINT,
        VK_IMAGE_USAGE_SAMPLED_BIT,
        [state](void* mapped_data) {
            uint64_t* output = (uint64_t*)mapped_data;
            for (uint32_t zindex = 0; zindex < NODE_SIZE; ++zindex)
            {
                for (uint32_t yindex = 0; yindex < NODE_SIZE; ++yindex)
                {
                    for (uint32_t xindex = 0; xindex < NODE_SIZE; ++xindex)
                    {
                        numtk::vec3u base{
                            xindex * 4 * SCALE_RATIO,
                            yindex * 4 * SCALE_RATIO,
                            zindex * 4 * SCALE_RATIO
                        };

                        uint64_t bits = state->voxel_region.ExtractKernel(base);
                        output[xindex + yindex*NODE_SIZE + zindex*NODE_SIZE*NODE_SIZE] = bits;
                    }
                }
            }
        });

#if 0
    for (uint32_t index = 0; index < 64; ++index)
        state->tilemap_region.Set(numtk::vec3u{ 252 + (index % 8), 256, 252 + (index / 8) }, (uint8_t)1);
#else
    for (uint32_t index = 0; index < 512*512; ++index)
        state->tilemap_region.Set(numtk::vec3u{ 0 + (index % 512), 0, 0 + (index / 512) }, (uint8_t)1);
#endif

    state->tilemap = vulkan->CreateTexture(
        NODE_SIZE, NODE_SIZE, NODE_SIZE,
        VK_IMAGE_TYPE_3D, VK_FORMAT_R32G32_UINT,
        VK_IMAGE_USAGE_SAMPLED_BIT,
        [state](void* mapped_data) {
            uint64_t* output = (uint64_t*)mapped_data;
            for (uint32_t zindex = 0; zindex < NODE_SIZE; ++zindex)
            {
                for (uint32_t yindex = 0; yindex < NODE_SIZE; ++yindex)
                {
                    for (uint32_t xindex = 0; xindex < NODE_SIZE; ++xindex)
                    {
                        numtk::vec3u base{
                            xindex * 4 * SCALE_RATIO,
                            yindex * 4 * SCALE_RATIO,
                            zindex * 4 * SCALE_RATIO
                        };

                        uint64_t bits = state->tilemap_region.ExtractKernel(base);
                        output[xindex + yindex*NODE_SIZE + zindex*NODE_SIZE*NODE_SIZE] = bits;
                    }
                }
            }
        });

    state->nodes_buffer = vulkan->CreateBuffer(
        sizeof(float),
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

#if 0
    vktk::CommandBuffer update_commands = vulkan->CreateCommandBuffer(vulkan->present_queue);
    update_commands.Begin();
    update_commands.ImageTransitions(
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        {
            {
                state->sparse_texture.image,
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_IMAGE_ASPECT_COLOR_BIT
            }
        });
    update_commands.End();
    update_commands.Submit();
#endif
}

void ConstantResources(void* _pstate, vktk::Context* _vulkan)
{
    voxel_state_t* _state = (voxel_state_t*)_pstate;

    _state->forward_graph = FullFrameRenderGraph();
    _state->forward_runtime.vulkan = _vulkan;
    _state->forward_runtime.graph = &_state->forward_graph;

    _state->pixel_cycling_graph = PixelCyclingRenderGraph();
    _state->pixel_cycling_runtime.vulkan = _vulkan;
    _state->pixel_cycling_runtime.graph = &_state->pixel_cycling_graph;

    _state->sparse_graph = SparseRegionRenderGraph();
    _state->sparse_runtime.vulkan = _vulkan;
    _state->sparse_runtime.graph = &_state->sparse_graph;

    //_state->region = DeserialiseRegion("../r.-1.6.mca");
    _state->voxel_region = DeserialiseRegion("../r.-1.6.mca");
    LoadRegion(_state, _vulkan);
    LoadArea(_state, _vulkan, { 0, 0, 0 }, { 32, 16, 32 });
    LoadRegionSparse(_state, _vulkan);

    for (DispatchNodeInfo const& node : _state->sparse_graph.nodes)
    {
        PNextMap pNext_map = {};

        if (node.name == "SparseRegionDispatch")
        {
            static uint32_t desc_counts[2] = { 0u, 0u };
            desc_counts[0] = (uint32_t)_state->masks.size();//65;
            desc_counts[1] = (uint32_t)_state->offsets.size();//65;

            static VkDescriptorSetVariableDescriptorCountAllocateInfo desc_count_allocs[2];
            desc_count_allocs[0] = VkDescriptorSetVariableDescriptorCountAllocateInfo{};
            desc_count_allocs[1] = VkDescriptorSetVariableDescriptorCountAllocateInfo{};

            desc_count_allocs[0].sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO;
            desc_count_allocs[0].pNext = nullptr;
            desc_count_allocs[0].descriptorSetCount = 1;
            desc_count_allocs[0].pDescriptorCounts = &desc_counts[0];

            desc_count_allocs[1].sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO;
            desc_count_allocs[1].pNext = nullptr;
            desc_count_allocs[1].descriptorSetCount = 1;
            desc_count_allocs[1].pDescriptorCounts = &desc_counts[1];

            //void* pnext_begin = &desc_count_alloc;

            pNext_map.emplace("sparse_region", std::vector<PNextEntry>{
                PNextEntry{ 1, (void*)&desc_count_allocs[0] },
                PNextEntry{ 2, (void*)&desc_count_allocs[1] }
            });
        }

        _state->sparse_runtime.nodes_static.push_back(
            GenerateDispatchStaticRuntime(node, _vulkan, pNext_map)
            );
    }

    for (DispatchNodeInfo const& node : _state->forward_graph.nodes)
    {
        PNextMap pNext_map = {};

        if (node.name == "MainWorldDispatch")
        {
            static uint32_t desc_count = 0u;
            static VkDescriptorSetVariableDescriptorCountAllocateInfo desc_count_alloc = {};
            desc_count = (uint32_t)_state->section_textures.size();
            desc_count_alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO;
            desc_count_alloc.pNext = nullptr;
            desc_count_alloc.descriptorSetCount = 1;
            desc_count_alloc.pDescriptorCounts = &desc_count;
            void* pnext_begin = &desc_count_alloc;

            pNext_map.emplace("render_region", std::vector<PNextEntry>{
                PNextEntry{ 0, (void*)&desc_count_alloc }
            });
        }

        _state->forward_runtime.nodes_static.push_back(
            GenerateDispatchStaticRuntime(node, _vulkan, pNext_map)
            );
    }

    for (DispatchNodeInfo const& node : _state->pixel_cycling_graph.nodes)
    {
        PNextMap pNext_map = {};

        if (node.name == "MainWorldDispatch")
        {
            static uint32_t desc_count = 0u;
            static VkDescriptorSetVariableDescriptorCountAllocateInfo desc_count_alloc = {};
            desc_count = (uint32_t)_state->section_textures.size();
            desc_count_alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO;
            desc_count_alloc.pNext = nullptr;
            desc_count_alloc.descriptorSetCount = 1;
            desc_count_alloc.pDescriptorCounts = &desc_count;
            void* pnext_begin = &desc_count_alloc;

            pNext_map.emplace("render_region", std::vector<PNextEntry>{
                PNextEntry{ 0, (void*)&desc_count_alloc }
            });
        }

        _state->pixel_cycling_runtime.nodes_static.push_back(
            GenerateDispatchStaticRuntime(node, _vulkan, pNext_map)
            );
    }

    _state->staging_buffer = _vulkan->CreateBuffer(
        sizeof(MatrixBlock) * 64,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
        | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    _state->matrix_buffer = _vulkan->CreateBuffer(
        sizeof(MatrixBlock) * 64,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT
        | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);

    _state->staging_area = nullptr;
    CHECKCALL(vkMapMemory,
              _vulkan->device, _state->staging_buffer.alloc.memory,
              _state->staging_buffer.alloc.offset, _state->staging_buffer.alloc.size,
              0, &_state->staging_area);

    MatrixBlock* matrices = (MatrixBlock*)_state->staging_area;
    for (uint32_t index = 0; index < 64; ++index)
    {
        matrices[index].projection = numtk::mat4f::Constant(0.f);
        matrices[index].view_model = numtk::mat4f::Constant(0.f);
        matrices[index].view = numtk::mat4f::Constant(0.f);
        matrices[index].screen_size = numtk::vec2<int32_t>::Constant(1);
    }

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
    sampler_create.maxLod = 4.0;
    CHECKCALL(vkCreateSampler, _vulkan->device,
              &sampler_create, nullptr, &_state->sampler);

    VkDescriptorImageInfo region_image_info{};
    region_image_info.sampler = _state->sampler;
    region_image_info.imageView = _state->region_texture.view;
    region_image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    {
        DispatchNodeInfo const& main_world_node =
            *std::find_if(_state->forward_graph.nodes.begin(), _state->forward_graph.nodes.end(),
                          [](DispatchNodeInfo const& node) {
                              return node.name == "MainWorldDispatch";
                          });

        for (uint32_t index = 0; index < 64; ++index)
        {
            std::vector<VkWriteDescriptorSet> desc_writes = {};

            VkWriteDescriptorSet region_binding =
                _state->forward_runtime.BindShaderResource(
                    NodeBindingPoint{
                        .node = &main_world_node,
                        .stage_index = 0,
                        .type = NodeBindingPoint::BindingType_Shader,
                        .shader = ShaderBindingInfo{ 0, 0 }
                    },
                    index,
                    ShaderResourceBinding{
                        .descriptor_count = 1,
                        .images = &region_image_info
                    });

            VkDescriptorBufferInfo buffer_info = {};
            buffer_info.buffer = _state->matrix_buffer;
            buffer_info.offset = sizeof(MatrixBlock) * index;
            buffer_info.range = sizeof(MatrixBlock);
            VkWriteDescriptorSet matrix_binding =
                _state->forward_runtime.BindShaderResource(
                    NodeBindingPoint{
                        .node = &main_world_node,
                        .stage_index = 0,
                        .type = NodeBindingPoint::BindingType_Shader,
                        .shader = ShaderBindingInfo{ 0, 1 }
                    },
                    index,
                    ShaderResourceBinding{
                        .descriptor_count = 1,
                        .buffers = &buffer_info
                    });

            std::vector<VkDescriptorImageInfo> section_images_info(_state->section_textures.size());
            for (uint32_t section_index = 0u;
                 section_index < _state->section_textures.size();
                 ++section_index)
            {
                VkDescriptorImageInfo& image_info = section_images_info[section_index];
                image_info.sampler = _state->sampler;
                image_info.imageView = _state->section_textures[section_index].view;
                image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            }

            VkWriteDescriptorSet section_bindings =
                _state->forward_runtime.BindShaderResource(
                    NodeBindingPoint{
                        .node = &main_world_node,
                        .stage_index = 0,
                        .type = NodeBindingPoint::BindingType_Shader,
                        .shader = ShaderBindingInfo{ 0, 2 }
                    },
                    index,
                    ShaderResourceBinding{
                        .descriptor_count = (uint32_t)section_images_info.size(),
                        .images = section_images_info.data()
                    });

            desc_writes.push_back(region_binding);
            desc_writes.push_back(matrix_binding);
            desc_writes.push_back(section_bindings);

            vkUpdateDescriptorSets(_vulkan->device, desc_writes.size(), desc_writes.data(), 0u, nullptr);
        }
    }

    {
        DispatchNodeInfo const& main_world_node =
            *std::find_if(_state->pixel_cycling_graph.nodes.begin(), _state->pixel_cycling_graph.nodes.end(),
                          [](DispatchNodeInfo const& node) {
                              return node.name == "MainWorldDispatch";
                          });

        for (uint32_t index = 0; index < 64; ++index)
        {
            std::vector<VkWriteDescriptorSet> desc_writes = {};

            VkWriteDescriptorSet region_binding =
                _state->pixel_cycling_runtime.BindShaderResource(
                    NodeBindingPoint{
                        .node = &main_world_node,
                        .stage_index = 0,
                        .type = NodeBindingPoint::BindingType_Shader,
                        .shader = ShaderBindingInfo{ 0, 0 }
                    },
                    index,
                    ShaderResourceBinding{
                        .descriptor_count = 1,
                        .images = &region_image_info
                    });

            VkDescriptorBufferInfo buffer_info = {};
            buffer_info.buffer = _state->matrix_buffer;
            buffer_info.offset = sizeof(MatrixBlock) * index;
            buffer_info.range = sizeof(MatrixBlock);
            VkWriteDescriptorSet matrix_binding =
                _state->pixel_cycling_runtime.BindShaderResource(
                    NodeBindingPoint{
                        .node = &main_world_node,
                        .stage_index = 0,
                        .type = NodeBindingPoint::BindingType_Shader,
                        .shader = ShaderBindingInfo{ 0, 1 }
                    },
                    index,
                    ShaderResourceBinding{
                        .descriptor_count = 1,
                        .buffers = &buffer_info
                    });

            std::vector<VkDescriptorImageInfo> section_images_info(_state->section_textures.size());
            for (uint32_t section_index = 0u;
                 section_index < _state->section_textures.size();
                 ++section_index)
            {
                VkDescriptorImageInfo& image_info = section_images_info[section_index];
                image_info.sampler = _state->sampler;
                image_info.imageView = _state->section_textures[section_index].view;
                image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            }

            VkWriteDescriptorSet section_bindings =
                _state->pixel_cycling_runtime.BindShaderResource(
                    NodeBindingPoint{
                        .node = &main_world_node,
                        .stage_index = 0,
                        .type = NodeBindingPoint::BindingType_Shader,
                        .shader = ShaderBindingInfo{ 0, 2 }
                    },
                    index,
                    ShaderResourceBinding{
                        .descriptor_count = (uint32_t)section_images_info.size(),
                        .images = section_images_info.data()
                    });

            desc_writes.push_back(region_binding);
            desc_writes.push_back(matrix_binding);
            desc_writes.push_back(section_bindings);

            vkUpdateDescriptorSets(_vulkan->device, desc_writes.size(), desc_writes.data(), 0u, nullptr);
        }
    }

    {
        DispatchNodeInfo const& sparse_region_node =
            *std::find_if(_state->sparse_graph.nodes.begin(), _state->sparse_graph.nodes.end(),
                          [](DispatchNodeInfo const& node) {
                              return node.name == "SparseRegionDispatch";
                          });

        std::vector<VkWriteDescriptorSet> bindings = {};

        std::vector<VkDescriptorImageInfo> nodes_info{};
        for (vktk::Texture const& node : _state->masks)
        {
            VkDescriptorImageInfo region_image_info{};
            region_image_info.sampler = _state->sampler;
            region_image_info.imageView = node.view;
            region_image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            nodes_info.push_back(region_image_info);
        }

        bindings.push_back(
            _state->sparse_runtime.BindShaderResource(
                NodeBindingPoint{
                    .node = &sparse_region_node,
                    .stage_index = 0,
                    .type = NodeBindingPoint::BindingType_Shader,
                    .shader = ShaderBindingInfo{ 1, 0 }
                },
                0,
                ShaderResourceBinding{
                    .descriptor_count = (uint32_t)nodes_info.size(),
                    .images = nodes_info.data()
                })
            );

        std::vector<VkDescriptorImageInfo> offsets_info{};
        for (vktk::Texture const& offsets : _state->offsets)
        {
            VkDescriptorImageInfo offsets_image_info{};
            offsets_image_info.sampler = _state->sampler;
            offsets_image_info.imageView = offsets.view;
            offsets_image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            offsets_info.push_back(offsets_image_info);
        }

        bindings.push_back(
            _state->sparse_runtime.BindShaderResource(
                NodeBindingPoint{
                    .node = &sparse_region_node,
                    .stage_index = 0,
                    .type = NodeBindingPoint::BindingType_Shader,
                    .shader = ShaderBindingInfo{ 2, 0 }
                },
                0,
                ShaderResourceBinding{
                    .descriptor_count = (uint32_t)offsets_info.size(),
                    .images = offsets_info.data()
                })
            );

#if 0
        std::vector<VkDescriptorImageInfo> nodes_info{};
        {
            VkDescriptorImageInfo info = {};
            info.sampler = _state->sampler;
            info.imageView = _state->tilemap.view;
            info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            nodes_info.push_back(info);
        }

        for (uint32_t index = 0u; index < 64; ++index)
        {
            VkDescriptorImageInfo region_image_info{};
            region_image_info.sampler = _state->sampler;
            region_image_info.imageView = _state->sparse_texture.view;
            region_image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            nodes_info.push_back(region_image_info);
        }

        bindings.push_back(
            _state->sparse_runtime.BindShaderResource(
                NodeBindingPoint{
                    .node = &sparse_region_node,
                    .stage_index = 0,
                    .type = NodeBindingPoint::BindingType_Shader,
                    .shader = ShaderBindingInfo{ 1, 0 }
                },
                0,
                ShaderResourceBinding{
                    .descriptor_count = (uint32_t)nodes_info.size(),
                    .images = nodes_info.data()
                })
            );
#endif

        VkDescriptorBufferInfo matrix_buffer_info = {};
        matrix_buffer_info.buffer = _state->matrix_buffer;
        matrix_buffer_info.offset = 0;
        matrix_buffer_info.range = sizeof(MatrixBlock);
        bindings.push_back(
            _state->sparse_runtime.BindShaderResource(
                NodeBindingPoint{
                    .node = &sparse_region_node,
                    .stage_index = 0,
                    .type = NodeBindingPoint::BindingType_Shader,
                    .shader = ShaderBindingInfo{ 0, 1 }
                },
                0,
                ShaderResourceBinding{
                    .descriptor_count = 1,
                    .buffers = &matrix_buffer_info
                })
            );

        VkDescriptorBufferInfo nodes_buffer_info = {};
        nodes_buffer_info.buffer = _state->nodes_buffer;
        nodes_buffer_info.offset = 0;
        nodes_buffer_info.range = sizeof(float);
        bindings.push_back(
            _state->sparse_runtime.BindShaderResource(
                NodeBindingPoint{
                    .node = &sparse_region_node,
                    .stage_index = 0,
                    .type = NodeBindingPoint::BindingType_Shader,
                    .shader = ShaderBindingInfo{ 0, 0 }
                },
                0,
                ShaderResourceBinding{
                    .descriptor_count = 1,
                    .buffers = &nodes_buffer_info
                })
            );

        vkUpdateDescriptorSets(_vulkan->device, bindings.size(), bindings.data(), 0u, nullptr);
    }

    vktk::CommandBuffer command_buffer = _vulkan->CreateCommandBuffer(_vulkan->present_queue);
    command_buffer.Begin();

    command_buffer.BufferTransitions(
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        {
            { _state->matrix_buffer,
              VK_ACCESS_NONE,
              VK_ACCESS_TRANSFER_WRITE_BIT },
        });

    command_buffer.BufferCopy(_state->matrix_buffer, 0, _state->staging_buffer, 0,
                              sizeof(MatrixBlock) * 64);

    command_buffer.BufferTransitions(
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
        {
            { _state->matrix_buffer,
              VK_ACCESS_TRANSFER_WRITE_BIT,
              VK_ACCESS_UNIFORM_READ_BIT }
        });

    command_buffer.End();
    command_buffer.Submit();

    return;
}

void RenderResources(void* _pstate,
                     vktk::Context* _vulkan,
                     vktk::Swapchain const& _swapchain)
{
    voxel_state_t* _state = (voxel_state_t*)_pstate;

    {
        _state->forward_runtime.ClearFramebufferRuntime();
        _state->forward_runtime.GenerateFramebufferRuntime(&_swapchain.format);

        DispatchNodeInfo const& main_world_node =
            *std::find_if(_state->forward_graph.nodes.begin(), _state->forward_graph.nodes.end(),
                          [](DispatchNodeInfo const& node) {
                              return node.name == "MainWorldDispatch";
                          });

        _state->forward_runtime.BindSwapchain(&main_world_node, _swapchain);
    }

    {
        _vulkan->DestroyTexture(_state->primary_pass_color);
        _state->primary_pass_color = _vulkan->CreateTexture(
            _swapchain.size.width, _swapchain.size.height, 1,
            VK_IMAGE_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
            | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
            | VK_IMAGE_USAGE_SAMPLED_BIT
        );

        _vulkan->DestroyTexture(_state->primary_pass_depth);
        _state->primary_pass_depth = _vulkan->CreateTexture(
            _swapchain.size.width, _swapchain.size.height, 1,
            VK_IMAGE_TYPE_2D, VK_FORMAT_D32_SFLOAT,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
        );
    }

    {
        _state->pixel_cycling_runtime.ClearFramebufferRuntime();
        _state->pixel_cycling_runtime.GenerateFramebufferRuntime(&_swapchain.format);

        DispatchNodeInfo const& main_world_node =
            *std::find_if(_state->pixel_cycling_graph.nodes.begin(), _state->pixel_cycling_graph.nodes.end(),
                          [](DispatchNodeInfo const& node) {
                              return node.name == "MainWorldDispatch";
                          });

        DispatchNodeInfo const& resolve_node =
            *std::find_if(_state->pixel_cycling_graph.nodes.begin(), _state->pixel_cycling_graph.nodes.end(),
                          [](DispatchNodeInfo const& node) {
                              return node.name == "ResolveDispatch";
                          });

        numtk::vec2u size = { _swapchain.size.width, _swapchain.size.height };
        _state->pixel_cycling_runtime.BindFramebufferAttachments(
            &main_world_node, size,
            { _state->primary_pass_color.view, _state->primary_pass_depth.view }
            );
        _state->pixel_cycling_runtime.BindSwapchain(&resolve_node, _swapchain);

        VkDescriptorImageInfo image_info{};
        image_info.sampler = _state->sampler;
        image_info.imageView = _state->primary_pass_color.view;
        image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet desc_write = _state->pixel_cycling_runtime.BindShaderResource(
            NodeBindingPoint{
                .node = &resolve_node,
                .stage_index = 0,
                .type = NodeBindingPoint::BindingType_Shader,
                .shader = ShaderBindingInfo{ 0, 0 }
            },
            0,
            ShaderResourceBinding{
                .descriptor_count = 1,
                .images = &image_info
            });
        vkUpdateDescriptorSets(_vulkan->device, 1, &desc_write, 0u, nullptr);

        numtk::vec2i screen_size{ (int32_t)_swapchain.size.width, (int32_t)_swapchain.size.height };
        numtk::vec2i* push_constants =
            (numtk::vec2i*)_state->pixel_cycling_runtime.BindPushConstants(
                NodeBindingPoint{
                    .node = &resolve_node,
                    .stage_index = 0,
                    .type = NodeBindingPoint::BindingType_PushConstantRange,
                    .push_constants = PushConstantsBindingInfo{ 0 }
                }, 0);
        *push_constants = screen_size;
    }

    {
        _state->sparse_runtime.ClearFramebufferRuntime();
        _state->sparse_runtime.GenerateFramebufferRuntime(&_swapchain.format);

        DispatchNodeInfo const& sparse_region_node =
            *std::find_if(_state->sparse_graph.nodes.begin(), _state->sparse_graph.nodes.end(),
                          [](DispatchNodeInfo const& node) {
                              return node.name == "SparseRegionDispatch";
                          });

        _state->sparse_runtime.BindSwapchain(&sparse_region_node, _swapchain);
    }

    {
        vktk::CommandBuffer layout_commands = _vulkan->CreateCommandBuffer(_vulkan->present_queue);
        layout_commands.Begin();

        layout_commands.ImageTransitions(
            VK_PIPELINE_STAGE_NONE,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            {
                {
                    _state->primary_pass_color.image,
                    VK_ACCESS_NONE, VK_ACCESS_NONE,
                    VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_IMAGE_ASPECT_COLOR_BIT
                },
                {
                    _state->primary_pass_depth.image,
                    VK_ACCESS_NONE, VK_ACCESS_NONE,
                    VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                    VK_IMAGE_ASPECT_DEPTH_BIT
                }
            });

        layout_commands.End();
        layout_commands.Submit();
    }
}

bool Update(void* _pstate, vktk::Context* _vulkan,
            iotk::input_t const* _input,
            iotk::input_t const* _past_input)
{
    voxel_state_t* _state = (voxel_state_t*)_pstate;

    std::cout << _input->time_delta << std::endl;
    _state->time += _input->time_delta;

    if (_input->key_down[iotk::eKey::kEscape])
        return false;

    if (_input->button_down & iotk::fMouseButton::kRightBtn)
    {
        numtk::vec2<int32_t> last_position{ _past_input->cursor[0], _past_input->cursor[1] };
        numtk::vec2<int32_t> position{ _input->cursor[0], _input->cursor[1] };
        numtk::vec2<int32_t> delta = position - last_position;
        _state->sun_spherical += numtk::vec2f{
            0.01f * (float)delta.x(),
            -0.01f * (float)delta.y(),
        };

        _state->sun_spherical.y() =
            std::min(std::max(_state->sun_spherical.y(), -numtk::kPi*0.4999f), numtk::kPi*0.4999f);
    }

    if (!_input->key_down['c'] && _past_input->key_down['c'])
        _state->pixel_cycling_enabled = !_state->pixel_cycling_enabled;

    _state->camera.Update(_input, _past_input);

    return true;
}

void Draw(void* _pstate,
          vktk::Context* _vulkan,
          vktk::Swapchain const& _swapchain,
          VkSemaphore _acquire_semaphore)
{
    voxel_state_t* _state = (voxel_state_t*)_pstate;

    numtk::vec2u const sample_index{ (_state->frame_index%4)/2, _state->frame_index%2 };
    ++_state->frame_index;

    numtk::bounds2u scissor{ { 0, 0 }, { _swapchain.size.width, _swapchain.size.height } };
    numtk::bounds2f viewport{ { 0.f, 0.f }, { (float)scissor.extent.x(), (float)scissor.extent.y() } };

    if (_state->pixel_cycling_enabled)
    {
        _state->current_runtime = &_state->pixel_cycling_runtime;

        scissor = numtk::bounds2u{
            { ((_swapchain.size.width) / 2) * sample_index.x(),
              ((_swapchain.size.height) / 2) * sample_index.y() },
            { (_swapchain.size.width) / 2,
              (_swapchain.size.height) / 2 },
        };

        viewport = numtk::bounds2f{
            { 0.f + float(sample_index.x()) * ((float)scissor.extent.x() - 0.5f),
              0.f + float(sample_index.y()) * ((float)scissor.extent.y() - 0.5f) },
            { (float)scissor.extent.x(),
              (float)scissor.extent.y() }
        };
    }
    else
        _state->current_runtime = &_state->forward_runtime;

    numtk::bounds2u render_area = scissor;

    float how = (float)_swapchain.size.height / (float)_swapchain.size.width;

    MatrixBlock* matrices = (MatrixBlock*)_state->staging_area;
    for (uint32_t index = 0; index < 64; ++index)
    {
        matrices[index].view = _state->camera.Matrix();

#if defined(TRAVERSAL_V1)
        matrices[index].projection =
            numtk::mat4f::Perspective_VulkanNDC(0.1f, 1000.f, 90.f*numtk::kDegrees, how);

        matrices[index].view_model =
            matrices[index].view
            * numtk::mat4f{
            2.f, 0.f, 0.f, 0.f,
            0.f, 2.f, 0.f, 0.f,
            0.f, 0.f, 2.f, 0.f,
            0.f, 0.f, 0.f, 1.f}
            * numtk::mat4f{
            1.f, 0.f, 0.f, 0.f,
            0.f, 1.f, 0.f, 0.f,
            0.f, 0.f, 1.f, 0.f,
            (float)(index/8) * 2.f, 0.f, (float)(index%8) * 2.f, 1.f};

#elif defined(TRAVERSAL_V2)
        matrices[index].projection =
            numtk::mat4f::Perspective_VulkanNDC(1.0f, 1000000.f, 90.f*numtk::kDegrees, how);

        matrices[index].view_model =
            matrices[index].view
            * numtk::mat4f{
            131072.f, 0.f, 0.f, 0.f,
            0.f, 131072.f, 0.f, 0.f,
            0.f, 0.f, 131072.f, 0.f,
            0.f, 0.f, 0.f, 1.f}
            * numtk::mat4f{
            1.f, 0.f, 0.f, 0.f,
            0.f, 1.f, 0.f, 0.f,
            0.f, 0.f, 1.f, 0.f,
            (float)(index/8) * 2.f, 0.f, (float)(index%8) * 2.f, 1.f};
#endif

        matrices[index].screen_offset = viewport.min;
        matrices[index].screen_size = {
            (int32_t)render_area.extent.x(),
            (int32_t)render_area.extent.y()
        };
    }

    vktk::CommandBuffer command_buffer = _vulkan->CreateCommandBuffer(_vulkan->present_queue);
    command_buffer.Begin();

    command_buffer.BufferTransitions(
        VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        {
            { _state->matrix_buffer, VK_ACCESS_UNIFORM_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT }
        });

    command_buffer.BufferCopy(_state->matrix_buffer, 0,
                              _state->staging_buffer, 0,
                              sizeof(MatrixBlock) * 64);

    command_buffer.BufferTransitions(
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
        {
            { _state->matrix_buffer, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_UNIFORM_READ_BIT }
        });

    numtk::vec3f sun_dir{
        std::cos(_state->sun_spherical[1]) * std::cos(_state->sun_spherical[0]),
        std::sin(_state->sun_spherical[1]),
        std::cos(_state->sun_spherical[1]) * std::sin(_state->sun_spherical[0])
    };

    SkyboxPushConstants skybox_params = {};
    skybox_params.projection = numtk::mat4f::Perspective_VulkanNDC(0.1f, 100.f, 90.f*numtk::kDegrees, how);
    skybox_params.view = _state->camera.Matrix();
    skybox_params.position = numtk::vec4f{ 0.f, 20.f, 0.f };
    skybox_params.light_dir = sun_dir;

    {
        DispatchNodeInfo const& main_world_node =
            *std::find_if(_state->current_runtime->graph->nodes.begin(),
                          _state->current_runtime->graph->nodes.end(),
                          [](DispatchNodeInfo const& node) {
                              return node.name == "MainWorldDispatch";
                          });

        _state->current_runtime->BindFramebufferArea(
            &main_world_node,
            FramebufferAreaBinding{
                !_state->pixel_cycling_enabled ? _swapchain.index : 0,
                scissor,
                viewport,
            });

        {
            GraphicsCommandBinding command = {};
            command.command = GraphicsCommandBinding::DispatchCommand_Draw;
            command.vertex_count = 36;
            command.instance_count = 1;

            for (uint32_t index = 0; index < 64; ++index)
            {
                _state->current_runtime->BindGraphicsCommand(
                    &main_world_node,
                    0, index,
                    command);

                numtk::vec3f* push_constants =
                    (numtk::vec3f*)_state->current_runtime->BindPushConstants(
                        NodeBindingPoint{
                            .node = &main_world_node,
                            .stage_index = 0,
                            .type = NodeBindingPoint::BindingType_PushConstantRange,
                            .push_constants = PushConstantsBindingInfo{ 0 }
                        },
                        index);
                *push_constants = sun_dir;
            }
        }

        {
            GraphicsCommandBinding command = {};
            command.command = GraphicsCommandBinding::DispatchCommand_Draw;
            command.vertex_count = 3;
            command.instance_count = 1;

            _state->current_runtime->BindGraphicsCommand(
                &main_world_node,
                1, 0,
                command);

            SkyboxPushConstants* push_constants =
                (SkyboxPushConstants*)_state->current_runtime->BindPushConstants(
                    NodeBindingPoint{
                        .node = &main_world_node,
                        .stage_index = 1,
                        .type = NodeBindingPoint::BindingType_PushConstantRange,
                        .push_constants = PushConstantsBindingInfo{ 0 }
                    }, 0);
            *push_constants = skybox_params;
        }
    }

    if (_state->pixel_cycling_enabled)
    {
        DispatchNodeInfo const& resolve_node =
            *std::find_if(_state->current_runtime->graph->nodes.begin(),
                          _state->current_runtime->graph->nodes.end(),
                          [](DispatchNodeInfo const& node) {
                              return node.name == "ResolveDispatch";
                          });

        GraphicsCommandBinding command = {};
        command.command = GraphicsCommandBinding::DispatchCommand_Draw;
        command.vertex_count = 3;
        command.instance_count = 1;

        _state->current_runtime->BindGraphicsCommand(
            &resolve_node,
            0, 0,
            command);

        _state->current_runtime->BindFramebufferArea(
            &resolve_node,
            FramebufferAreaBinding{
                _swapchain.index,
                numtk::bounds2u{
                    { 0, 0 },
                    { _swapchain.size.width, _swapchain.size.height }
                },
                numtk::bounds2f{
                    { 0.f, 0.f },
                    { (float)_swapchain.size.width, (float)_swapchain.size.height }
                }
            });
    }

    {
        DispatchNodeInfo const& sparse_region_node =
            *std::find_if(_state->sparse_graph.nodes.begin(), _state->sparse_graph.nodes.end(),
                          [](DispatchNodeInfo const& node) {
                              return node.name == "SparseRegionDispatch";
                          });

        GraphicsCommandBinding command = {};
        command.command = GraphicsCommandBinding::DispatchCommand_Draw;
        command.vertex_count = 36;
        command.instance_count = 1;

        _state->sparse_runtime.BindGraphicsCommand(
            &sparse_region_node, 0, 0, command);

        _state->sparse_runtime.BindFramebufferArea(
            &sparse_region_node,
            FramebufferAreaBinding{
                _swapchain.index,
                numtk::bounds2u{
                    { 0, 0 },
                    { _swapchain.size.width, _swapchain.size.height }
                },
                numtk::bounds2f{
                    { 0.f, 0.f },
                    { (float)_swapchain.size.width, (float)_swapchain.size.height }
                }
            });
    }

#if defined(TRAVERSAL_V1)
    _state->current_runtime->Dispatch(command_buffer);
#elif defined(TRAVERSAL_V2)
    _state->sparse_runtime.Dispatch(command_buffer);
#endif

    command_buffer.End();

    if (_state->last_submit != VK_NULL_HANDLE)
        _vulkan->ScheduleFenceDestroy(_state->last_submit);

    VkPipelineStageFlags wait_dst_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    command_buffer.Submit(nullptr, &_acquire_semaphore, &wait_dst_stage, 1u,
                          &_state->last_submit);
#if 0
    std::cout << "[VoxelsDraw] submitted on fence "
              << std::hex << _state->last_submit << std::dec << std::endl;
#endif

    VkPresentInfoKHR present_info{};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.pNext = nullptr;
    present_info.waitSemaphoreCount = 0u;
    present_info.pWaitSemaphores = nullptr;
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &_swapchain.handle;
    present_info.pImageIndices = &_swapchain.index;
    present_info.pResults = nullptr;
    CHECKCALL(_vulkan->fp.QueuePresentKHR,
              _vulkan->present_queue, &present_info);
}

void Shutdown(void* _pstate,
              vktk::Context* _vulkan)
{
    voxel_state_t* _state = (voxel_state_t*)_pstate;
    _state->forward_runtime.ClearFramebufferRuntime();
    _state->forward_runtime.ClearStaticRuntime();

    _state->pixel_cycling_runtime.ClearFramebufferRuntime();
    _state->pixel_cycling_runtime.ClearStaticRuntime();

    vkDestroySampler(_vulkan->device, _state->sampler, nullptr);
    _state->sampler = VK_NULL_HANDLE;

    _vulkan->DestroyTexture(_state->primary_pass_color);
    _state->primary_pass_color.image = VK_NULL_HANDLE;
    _state->primary_pass_color.view = VK_NULL_HANDLE;

    _vulkan->DestroyTexture(_state->primary_pass_depth);
    _state->primary_pass_depth.image = VK_NULL_HANDLE;
    _state->primary_pass_depth.view = VK_NULL_HANDLE;

    _vulkan->DestroyTexture(_state->region_texture);
    _state->region_texture = vktk::Texture{};

    vkUnmapMemory(_vulkan->device, _state->staging_buffer.alloc.memory);

    _vulkan->DestroyBuffer(_state->matrix_buffer);
    _vulkan->DestroyBuffer(_state->staging_buffer);

    for (uint32_t index = 0; index < _state->section_textures.size(); ++index)
        _vulkan->DestroyTexture(_state->section_textures[index]);

    if (_state->last_submit != VK_NULL_HANDLE)
    {
        _vulkan->ScheduleFenceDestroy(_state->last_submit);
        _state->last_submit = VK_NULL_HANDLE;
    }
}

void TurntableCamera::Update(iotk::input_t const* _input,
                             iotk::input_t const* _past_input)
{
    if (_input->button_down & iotk::fMouseButton::kLeftBtn)
    {
        numtk::vec2<int32_t> last_position{ _past_input->cursor[0], _past_input->cursor[1] };
        numtk::vec2<int32_t> position{ _input->cursor[0], _input->cursor[1] };
        numtk::vec2<int32_t> delta = position - last_position;
        spherical_coordinates += numtk::vec3f{
            0.01f * (float)delta.x(),
            0.01f * (float)delta.y(),
            0.f
        };
    }

    {
        spherical_coordinates.z *= (1.f + 0.0005f * (float)_input->wheel_delta);
        if (spherical_coordinates.z < 0.01f)
            spherical_coordinates.z = 0.01f;
    }

    if (_input->button_down & iotk::fMouseButton::kRightBtn)
    {
        y_offset += 0.0005f * (float)(_input->cursor[1] -  _past_input->cursor[1]);
    }

    {
        numtk::vec3<int32_t> next_section = current_section;
        #define KEY_PRESSED(key) _input->key_down[key] && !_past_input->key_down[key]
        if (KEY_PRESSED('w'))
            ++next_section[0];
        if (KEY_PRESSED('s'))
            --next_section[0];
        if (KEY_PRESSED('d'))
            ++next_section[2];
        if (KEY_PRESSED('a'))
            --next_section[2];
        if (KEY_PRESSED('q'))
            --next_section[1];
        if (KEY_PRESSED('e'))
            ++next_section[1];
        #undef KEY_PRESSED

        next_section.x = std::max(0, std::min(31, next_section.x));
        next_section.y = std::max(0, std::min(23, next_section.y));
        next_section.z = std::max(0, std::min(31, next_section.z));

        current_section = next_section;
    }
}

numtk::mat4f TurntableCamera::Matrix() const
{
    numtk::vec3f forward = {
        std::cos(spherical_coordinates[1]) * std::cos(spherical_coordinates[0]),
        std::sin(spherical_coordinates[1]),
        std::cos(spherical_coordinates[1]) * std::sin(spherical_coordinates[0])
    };
    forward = forward.normalise();

    numtk::vec3f up = { 0.f, 1.f, 0.f };
    if (std::abs(1.f - forward.dot(numtk::vec3f{ 0.f, 1.f, 0.f })) > 1e-3f)
        up = forward.cross(numtk::vec3f{ 0.f, 1.f, 0.f }).cross(forward).normalise();
    else
        up = forward.cross(numtk::vec3f{ 0.f, 0.f, 1.f }).cross(forward).normalise();

    numtk::vec3f center = (numtk::vec3f{
        (float)current_section.x + 0.5f,
        (float)current_section.y + 0.5f,
        (float)current_section.z + 0.5f
    } / 16.f) - numtk::vec3f{ 1.f, 1.f, 1.f } + numtk::vec3f{ 0.f, y_offset, 0.f };

    return
        numtk::mat4f::View({ 0.f, 0.f, -1.f },
                           { 0.f, 1.f, 0.f },
                           { 0.f, 0.f, spherical_coordinates.z })
        * numtk::mat4f::View(forward, up, center);

}

void FlightCamera::Update(iotk::input_t const* _input,
                          iotk::input_t const* _past_input)
{
    {
        velocity *= (1.f + 0.005f * (float)_input->wheel_delta);
        if (velocity < 0.01f)
            velocity = 0.01f;
    }

    if (_input->button_down & iotk::fMouseButton::kLeftBtn)
    {
        numtk::vec2<int32_t> last_position{ _past_input->cursor[0], _past_input->cursor[1] };
        numtk::vec2<int32_t> position{ _input->cursor[0], _input->cursor[1] };
        numtk::vec2<int32_t> delta = position - last_position;
        forward_spherical += numtk::vec2f{
            0.01f * (float)delta.x(),
            -0.01f * (float)delta.y(),
        };

        forward_spherical.y() =
            std::min(std::max(forward_spherical.y(), -numtk::kPi*0.4999f), numtk::kPi*0.4999f);
    }

    numtk::vec3f forward = {
        std::cos(forward_spherical[1]) * std::cos(forward_spherical[0]),
        std::sin(forward_spherical[1]),
        std::cos(forward_spherical[1]) * std::sin(forward_spherical[0])
    };
    forward = forward.normalise();

    numtk::vec3f left = numtk::vec3f{ 0.f, 1.f, 0.f }.cross({
        std::cos(forward_spherical[0]),
        0.f,
        std::sin(forward_spherical[0])
    });

    numtk::vec3f movement = { 0.f, 0.f, 0.f };
    if (_input->key_down['w'])
        movement.z += 1.f;
    if (_input->key_down['s'])
        movement.z -= 1.f;
    if (_input->key_down['a'])
        movement.x += 1.f;
    if (_input->key_down['d'])
        movement.x -= 1.f;
    if (_input->key_down[' '])
        movement.y += 1.f;
    if (_input->mod_down & iotk::fKeyMod::kShift)
        movement.y -= 1.f;
    movement.normalise();

    position += (forward*movement.z + left*movement.x + numtk::vec3f{ 0.f, movement.y, 0.f })
        * velocity * _input->time_delta;
}

numtk::mat4f FlightCamera::Matrix() const
{
    numtk::vec3f forward = {
        std::cos(forward_spherical[1]) * std::cos(forward_spherical[0]),
        std::sin(forward_spherical[1]),
        std::cos(forward_spherical[1]) * std::sin(forward_spherical[0])
    };
    forward = forward.normalise();

    numtk::vec3f up = { 0.f, 1.f, 0.f };
    if (std::abs(1.f - forward.dot(numtk::vec3f{ 0.f, 1.f, 0.f })) > 1e-3f)
        up = forward.cross(numtk::vec3f{ 0.f, 1.f, 0.f }).cross(forward).normalise();
    else
        up = forward.cross(numtk::vec3f{ 0.f, 0.f, 1.f }).cross(forward).normalise();

    return numtk::mat4f::View(forward, up, position);
}


}

namespace Voxels
{

Interface::Interface()
    : ConstantResources{}
    , RenderResources{}
    , Update{}
    , Draw{}
    , Shutdown{}
    , state{ new voxel_state_t{} }
{
    ReloadPointers();
}

Interface::~Interface()
{
    delete (voxel_state_t*)state;
}

void Interface::ReloadPointers()
{
    ConstantResources = ::ConstantResources;
    RenderResources = ::RenderResources;
    Update = ::Update;
    Draw = ::Draw;
    Shutdown = ::Shutdown;
}

}
