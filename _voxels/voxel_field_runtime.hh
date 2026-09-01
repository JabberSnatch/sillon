#pragma once

#include "dispatch_runtime.hh"

#include <numtk.hh>
#include <plltk.hh>
#include <voxtk.hh>

#include <memory>
#include <future>
#include <vector>

#define VOXEL_CHUNK_SIZE 16u

namespace voxel_field_runtime
{

struct RenderParams
{
    uint32_t chunk_size;
    uint32_t chunk_mask;
    uint32_t chunk_shift;
    uint32_t kernel_mask;
    uint32_t kernel_shift;
    numtk::vec3u indexing_masks;

    uint32_t NodeSize() const { return chunk_size * voxtk::VoxelMask::kKernelSize; }
    uint32_t ComponentShift() const { return chunk_shift + kernel_shift; }
    uint32_t LevelShift() const { return ComponentShift() * 3; }

    uint32_t PointIndex(numtk::vec3u const& _point) const { return
            numtk::BitDeposit(_point.x, indexing_masks.x)
            | numtk::BitDeposit(_point.y, indexing_masks.y)
            | numtk::BitDeposit(_point.z, indexing_masks.z);
    }

    numtk::vec3u IndexPoint(uint32_t _index) const { return {
            numtk::BitExtract(_index, indexing_masks.x),
            numtk::BitExtract(_index, indexing_masks.y),
            numtk::BitExtract(_index, indexing_masks.z),
        };
    }
};

struct FieldLevel
{
    std::vector<vktk::DeferredUpload> masks{};
    std::vector<vktk::DeferredUpload> offsets{};
    std::vector<vktk::DeferredUpload> payloads{};
};

struct FieldUpload
{
    voxtk::VoxelField const* base = nullptr;
    std::vector<voxtk::VoxelField> lods{};
    std::vector<FieldLevel> levels{};
    RenderParams render_params{};
    struct FieldDescriptor* package;
};

struct FieldDescriptor
{
    numtk::vec3f area_scale;
    numtk::vec3f area_size;
    int32_t stack_depth;
    int32_t display_depth;
    uint32_t leaves_begin;
    uint32_t residency_entry_count;
    std::vector<vktk::Texture> masks{};
    std::vector<vktk::Texture> offsets{};
    std::vector<vktk::Texture> payloads{};

    std::future<FieldUpload> upload = {};

    std::unique_ptr<plltk::Mutex> residency_mutex{ new plltk::Mutex{} };
    vktk::Buffer residency_buffer;
    vktk::Buffer residency_staging;
    std::vector<uint32_t> residency_state;
    std::unique_ptr<std::vector<vktk::DeferredUpload*>> pending_uploads{
        new std::vector<vktk::DeferredUpload*>{}
    };
};

struct Args
{
    FieldDescriptor* field_descriptor;
    numtk::vec2f framebuffer_size;
    numtk::mat4f view_matrix;
    numtk::mat4f projection_matrix;
    uint32_t sample_offset;
    numtk::vec2f sun_spherical;
    bool enable_indirect_light;
    float sun_power;
};

dispatch_runtime_t Create(vktk::Context* _vulkan, dstk::Registry* _registry);
void Destroy(dispatch_runtime_t&);

} // namespace voxel_field_runtime


namespace voxel_field_runtime
{

vkrg::layout::DispatchGraph RenderGraph(dstk::Registry& _registry);

struct MatrixBlock
{
    numtk::mat4f projection;
    numtk::mat4f view_model;
    numtk::mat4f view;
    numtk::vec2f screen_offset;
    numtk::vec2i screen_size;
    float padding[12];
};

struct SparseRegionPushConstants
{
    numtk::vec3f light_dir;
    float display_mode;
    numtk::vec3f region_scale;
    float node_size;
    int32_t stack_depth;
    uint32_t sample_offset;
    uint32_t leaves_begin;
    float sun_power;
};

struct Impl
{
    bool region_ready = false;
    VkSampler nearest_sampler;
    vktk::Buffer matrix_buffer;
    vktk::Buffer staging_buffer;

    void BindField(vkrg::runtime::DispatchContext& _context,
                   FieldDescriptor const& _descriptor);

    void OnModuleScopeOpen(vkrg::runtime::DispatchContext _context);
    void OnSwapchainScopeOpen(vkrg::runtime::DispatchContext _context,
                              vktk::Swapchain const& _swapchain);

    void UpdateBindings(vkrg::runtime::DispatchContext _context,
                        Args const& _args);
    void PostSubmit(vkrg::runtime::DispatchContext _context,
                    VkFence _fence);
};

void Impl::OnModuleScopeOpen(vkrg::runtime::DispatchContext _context)
{
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

    staging_buffer = _context.vulkan.CreateBuffer(
        sizeof(MatrixBlock),
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
        | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    matrix_buffer = _context.vulkan.CreateBuffer(
        sizeof(MatrixBlock),
        VK_BUFFER_USAGE_TRANSFER_DST_BIT
        | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);

    MatrixBlock* matrix = (MatrixBlock*)staging_buffer.alloc.map;
    matrix->projection = numtk::mat4f::Constant(0.f);
    matrix->view_model = numtk::mat4f::Constant(0.f);
    matrix->view = numtk::mat4f::Constant(0.f);
    matrix->screen_size = numtk::vec2<int32_t>::Constant(1);

    {
        vktk::CommandBuffer command_buffer =
            _context.vulkan.CreateCommandBuffer(_context.vulkan.present_queue);
        command_buffer.Begin();

        command_buffer.BufferTransitions(
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            {
                { matrix_buffer,
                  VK_ACCESS_NONE,
                  VK_ACCESS_TRANSFER_WRITE_BIT },
            });

        command_buffer.BufferCopy(matrix_buffer, 0, staging_buffer, 0,
                                  sizeof(MatrixBlock));

        command_buffer.BufferTransitions(
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
            {
                { matrix_buffer,
                  VK_ACCESS_TRANSFER_WRITE_BIT,
                  VK_ACCESS_UNIFORM_READ_BIT }
            });

        command_buffer.End();
        command_buffer.Submit();
    }
}

void Impl::OnSwapchainScopeOpen(vkrg::runtime::DispatchContext _context,
                                vktk::Swapchain const& _swapchain)
{}

void Impl::UpdateBindings(vkrg::runtime::DispatchContext _context,
                          Args const& _args)
{
    if (!_args.field_descriptor)
        return;

    FieldDescriptor& descriptor = *_args.field_descriptor;

    BindField(_context, descriptor);

    {
        descriptor.residency_mutex->Lock();

        if (descriptor.pending_uploads->empty())
            descriptor.residency_mutex->Unlock();
        else
        {
            for (auto upload : *descriptor.pending_uploads)
                _context.vulkan.CommitStaging(*upload);
            descriptor.pending_uploads->clear();

            std::memcpy(descriptor.residency_staging.alloc.map,
                        descriptor.residency_state.data(),
                        sizeof(uint32_t) * descriptor.residency_entry_count);

            vktk::CommandBuffer command_buffer =
                _context.vulkan.CreateCommandBuffer(_context.vulkan.present_queue);
            command_buffer.Begin();

            command_buffer.BufferTransitions(
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                {
                    { descriptor.residency_buffer,
                      VK_ACCESS_NONE,
                      VK_ACCESS_TRANSFER_WRITE_BIT },
                });

            command_buffer.BufferCopy(descriptor.residency_buffer, 0,
                                      descriptor.residency_staging, 0,
                                      sizeof(uint32_t) * descriptor.residency_entry_count);

            command_buffer.BufferTransitions(
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                {
                    { descriptor.residency_buffer,
                      VK_ACCESS_TRANSFER_WRITE_BIT,
                      VK_ACCESS_SHADER_READ_BIT }
                });

            command_buffer.End();

            VkFence sync_fence = VK_NULL_HANDLE;
            vktk::CommandBuffer::SubmitInfo submit_info = {};
            submit_info.signal_fence = &sync_fence;
            command_buffer.Submit(submit_info);

            _context.vulkan.ScheduleRoutine(
                sync_fence,
                [&descriptor](vktk::Context& _vulkan) {
                    descriptor.residency_mutex->Unlock();
                });
        }
    }

    {
        {
            MatrixBlock* matrix = (MatrixBlock*)staging_buffer.alloc.map;
            matrix->view = _args.view_matrix;
            matrix->projection = _args.projection_matrix;

            numtk::vec3f model_scale = descriptor.area_size;
            matrix->view_model =
                matrix->view
                * numtk::mat4f{
                model_scale.x, 0.f, 0.f, 0.f,
                0.f, model_scale.y, 0.f, 0.f,
                0.f, 0.f, model_scale.z, 0.f,
                0.f, 0.f, 0.f, 1.f };

            matrix->screen_offset = numtk::vec2f{ 0.f, 0.f };
            matrix->screen_size = _args.framebuffer_size.cast<int32_t>();

            vktk::CommandBuffer command_buffer =
                _context.vulkan.CreateCommandBuffer(_context.vulkan.present_queue);
            command_buffer.Begin();

            command_buffer.BufferTransitions(
                VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                {
                    { matrix_buffer,
                      VK_ACCESS_UNIFORM_READ_BIT,
                      VK_ACCESS_TRANSFER_WRITE_BIT }
                });

            command_buffer.BufferCopy(matrix_buffer, 0,
                                      staging_buffer, 0,
                                      sizeof(MatrixBlock));

            command_buffer.BufferTransitions(
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
                {
                    { matrix_buffer,
                      VK_ACCESS_TRANSFER_WRITE_BIT,
                      VK_ACCESS_UNIFORM_READ_BIT }
                });

            command_buffer.End();
            command_buffer.Submit();
        }

        numtk::vec3f sun_dir = numtk::vec3f{
            std::cos(_args.sun_spherical[1]) * std::cos(_args.sun_spherical[0]),
            std::sin(_args.sun_spherical[1]),
            std::cos(_args.sun_spherical[1]) * std::sin(_args.sun_spherical[0])
        }.normalise();

        dstk::Registry::Node sparse_region_node = _context.graph["SparseRegion"];

        vkrg::bind::GraphicsCommand command = {};
        command.command = vkrg::bind::DispatchCommand_Draw;
        command.vertex_count = 36;
        command.instance_count = 1;
        _context.BindGraphicsCommand(sparse_region_node, 0, command);

        SparseRegionPushConstants* push_constants =
            (SparseRegionPushConstants*)_context.BindPushConstants(
                sparse_region_node,
                {
                    .type = vkrg::layout::BindingType_PushConstantRange,
                    .push_constant_range = 0
                }, 0);
        push_constants->light_dir = sun_dir;
        push_constants->display_mode = _args.enable_indirect_light ? 1.f : 0.f;
        push_constants->node_size = (float)VOXEL_CHUNK_SIZE;
        push_constants->region_scale = descriptor.area_scale;
        push_constants->stack_depth = std::max(descriptor.display_depth-1, 0);
        push_constants->sample_offset = _args.sample_offset;
        push_constants->leaves_begin = descriptor.leaves_begin;
        push_constants->sun_power = _args.sun_power;
    }
}

void Impl::PostSubmit(vkrg::runtime::DispatchContext _context,
                      VkFence _fence)
{}

void Impl::BindField(vkrg::runtime::DispatchContext& _context,
                     FieldDescriptor const& _descriptor)
{
    {
        vkrg::bind::PNextMap pNext_map = {};

        static uint32_t desc_counts[3] = { 0u, 0u, 0u };
        desc_counts[0] = (uint32_t)_descriptor.masks.size();
        desc_counts[1] = (uint32_t)_descriptor.offsets.size();
        desc_counts[2] = (uint32_t)_descriptor.payloads.size();

        static VkDescriptorSetVariableDescriptorCountAllocateInfo desc_count_allocs[3];
        desc_count_allocs[0] = VkDescriptorSetVariableDescriptorCountAllocateInfo{};
        desc_count_allocs[1] = VkDescriptorSetVariableDescriptorCountAllocateInfo{};
        desc_count_allocs[2] = VkDescriptorSetVariableDescriptorCountAllocateInfo{};

        desc_count_allocs[0].sType =
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO;
        desc_count_allocs[0].pNext = nullptr;
        desc_count_allocs[0].descriptorSetCount = 1;
        desc_count_allocs[0].pDescriptorCounts = &desc_counts[0];

        desc_count_allocs[1].sType =
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO;
        desc_count_allocs[1].pNext = nullptr;
        desc_count_allocs[1].descriptorSetCount = 1;
        desc_count_allocs[1].pDescriptorCounts = &desc_counts[1];

        desc_count_allocs[2].sType =
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO;
        desc_count_allocs[2].pNext = nullptr;
        desc_count_allocs[2].descriptorSetCount = 1;
        desc_count_allocs[2].pDescriptorCounts = &desc_counts[2];

        pNext_map.emplace("SparseRegion", std::vector<vkrg::bind::PNextEntry>{
            { 1, (void*)&desc_count_allocs[0] },
            { 2, (void*)&desc_count_allocs[1] },
            { 3, (void*)&desc_count_allocs[2] }
        });

        _context.ResetDynamicRuntime(pNext_map);
    }

    dstk::Registry::Node sparse_region_node = _context.graph["SparseRegion"];

    std::vector<VkDescriptorImageInfo> nodes_info{};
    std::vector<VkDescriptorImageInfo> offsets_info{};
    std::vector<VkDescriptorImageInfo> payloads_info{};

    for (vktk::Texture const& node : _descriptor.masks)
    {
        VkDescriptorImageInfo region_image_info{};
        region_image_info.sampler = nearest_sampler;
        region_image_info.imageView = node.view;
        region_image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        nodes_info.push_back(region_image_info);
    }

    for (vktk::Texture const& node_payload : _descriptor.payloads)
    {
        VkDescriptorImageInfo payload_image_info{};
        payload_image_info.sampler = nearest_sampler;
        payload_image_info.imageView = node_payload.view;
        payload_image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        payloads_info.push_back(payload_image_info);
    }

    for (vktk::Texture const& offsets : _descriptor.offsets)
    {
        VkDescriptorImageInfo offsets_image_info{};
        offsets_image_info.sampler = nearest_sampler;
        offsets_image_info.imageView = offsets.view;
        offsets_image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        offsets_info.push_back(offsets_image_info);
    }

    VkDescriptorBufferInfo matrix_buffer_info = {};
    matrix_buffer_info.buffer = matrix_buffer;
    matrix_buffer_info.offset = 0;
    matrix_buffer_info.range = sizeof(MatrixBlock);

    std::vector<VkWriteDescriptorSet> desc_writes = {};
    desc_writes.push_back(
        _context.BindShaderResource(
            sparse_region_node,
            vkrg::layout::BindingPoint{
                .type = vkrg::layout::BindingType_Shader,
                .shader = { 0, 0 }
            },
            0,
            vkrg::bind::ShaderResources{
                .buffers = { matrix_buffer_info }
            })
    );

    VkDescriptorBufferInfo residency_buffer_info = {};
    residency_buffer_info.buffer = _descriptor.residency_buffer;
    residency_buffer_info.offset = 0;
    residency_buffer_info.range = _descriptor.residency_entry_count * sizeof(uint32_t);

    desc_writes.push_back(
        _context.BindShaderResource(
            sparse_region_node,
            vkrg::layout::BindingPoint{
                .type = vkrg::layout::BindingType_Shader,
                .shader = { 0, 1 }
            },
            0,
            vkrg::bind::ShaderResources{
                .buffers = { residency_buffer_info }
            })
    );

    desc_writes.push_back(
        _context.BindShaderResource(
            sparse_region_node,
            vkrg::layout::BindingPoint{
                .type = vkrg::layout::BindingType_Shader,
                .shader = { 1, 0 }
            },
            0,
            vkrg::bind::ShaderResources{
                .images = nodes_info
            })
    );

    desc_writes.push_back(
        _context.BindShaderResource(
            sparse_region_node,
            vkrg::layout::BindingPoint{
                .type = vkrg::layout::BindingType_Shader,
                .shader = { 2, 0 }
            },
            0,
            vkrg::bind::ShaderResources{
                .images = offsets_info
            })
    );

    desc_writes.push_back(
        _context.BindShaderResource(
            sparse_region_node,
            vkrg::layout::BindingPoint{
                .type = vkrg::layout::BindingType_Shader,
                .shader = { 3, 0 }
            },
            0,
            vkrg::bind::ShaderResources{
                .images = payloads_info
            })
    );

    vkUpdateDescriptorSets(_context.vulkan.device,
                           desc_writes.size(),
                           desc_writes.data(),
                           0u,
                           nullptr);
}

dispatch_runtime_t Create(vktk::Context* _vulkan, dstk::Registry* _registry)
{
    dispatch_runtime_t runtime = {};
    runtime.vulkan = _vulkan;
    runtime.registry = _registry;
    runtime.graph = RenderGraph(*_registry);
    runtime.impl = new Impl{};

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

void Destroy(dispatch_runtime_t& _rt)
{
    delete (Impl*)_rt.impl;
}

vkrg::layout::DispatchStage SparseRegionDispatchStageInfo()
{
    vkrg::layout::DispatchStage output = {};
    output.name = "sparse_region";

    output.shaders = std::vector<vkrg::layout::ShaderStage>{
        { VK_SHADER_STAGE_VERTEX_BIT,
          vktk::Context::CompileShader_GLSLtoSPIRV(
              VK_SHADER_STAGE_VERTEX_BIT, "sparse_region_vertex",
              { "#version 450",
                "#define CW_CUBE",
                #include "../shaders/cube_vertices.vert.h"
                ,
                #include "../shaders/draw_cube.vert.h"
              }
          )
        },
        { VK_SHADER_STAGE_FRAGMENT_BIT, {
            #include "shaders/sparse_region.frag.glsl.spv"
          }
        }
    };

    output.invocation_count = 1;

    output.binding_groups.push_back(vkrg::layout::StageBindingGroup{
        {
            { VK_SHADER_STAGE_ALL, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
              0, 1 },
            { VK_SHADER_STAGE_ALL, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
              0, 1 },
        }
    });

    output.binding_groups.push_back(vkrg::layout::StageBindingGroup{
        {
            { VK_SHADER_STAGE_FRAGMENT_BIT, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
              VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT, 512*128 },
        }
    });

    output.binding_groups.push_back(vkrg::layout::StageBindingGroup{
        {
            { VK_SHADER_STAGE_FRAGMENT_BIT, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
              VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT, 512*32 },
        }
    });

    output.binding_groups.push_back(vkrg::layout::StageBindingGroup{
        {
            { VK_SHADER_STAGE_FRAGMENT_BIT, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
              VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT, 512*128 },
        }
    });

    output.push_constants = std::vector<vkrg::layout::PushConstantRange>{
        { VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(SparseRegionPushConstants) }
    };

    output.graphics.state_overrides = std::vector<vktk::Context::StateOverride>{};
    output.graphics.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    output.graphics.input_layout = std::vector<vktk::Context::VertexInput>{};

    return output;
}

vkrg::layout::DispatchGraph RenderGraph(dstk::Registry& _registry)
{
    vkrg::layout::DispatchGraph output{ &_registry };
    _registry.BindComponent(output["SparseRegion"], SparseRegionDispatchStageInfo());
    return output;
}

} // namespace voxel_field_runtime
