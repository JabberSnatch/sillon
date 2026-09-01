#pragma once

#include "dispatch_runtime.hh"

#include "../guitk.hh"

namespace gui_runtime
{

struct Args
{
    guitk::WidgetStates const* gui_record;
    numtk::vec2f framebuffer_size;
};

dispatch_runtime_t Create(vktk::Context* _vulkan, dstk::Registry* _registry);

} // namespace gui_runtime

#include <algorithm>
#include <cstdint>

namespace gui_runtime
{

vkrg::layout::DispatchStage FramesDispatchStageInfo();
vkrg::layout::DispatchStage GlyphsDispatchStageInfo();

vkrg::layout::DispatchGraph RenderGraph(dstk::Registry& _registry);

struct Impl
{
    vktk::Buffer gui_frame_indices;
    vktk::Buffer gui_frames_vertices;
    vktk::Buffer gui_frame_uv;
    vktk::Buffer gui_glyph_indices;
    vktk::Buffer gui_glyph_vertices;

    vktk::Texture gui_font_texture;
    vktk::Texture gui_symbols_texture;
    VkSampler nearest_sampler;

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
    _context.vulkan.DestroyTexture(gui_font_texture);
    _context.vulkan.DestroyTexture(gui_symbols_texture);
    vkDestroySampler(_context.vulkan.device, nearest_sampler, nullptr);
}

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

    guitk::TextureData const& font_glyphs = guitk::FontBitmap();
    gui_font_texture = _context.vulkan.CreateTexture(
        font_glyphs.size.x(), font_glyphs.size.y(), 1,
        VK_IMAGE_TYPE_2D,
        VK_FORMAT_R8_UINT,
        VK_IMAGE_USAGE_SAMPLED_BIT,
        [&font_glyphs](void* buffer) {
            uint8_t* texture_buffer = (uint8_t*)buffer;
            std::copy(font_glyphs.pixels.begin(), font_glyphs.pixels.end(), texture_buffer);
        }
    );

    guitk::TextureData const& symbols = guitk::SymbolsBitmap();
    gui_symbols_texture = _context.vulkan.CreateTexture(
        symbols.size.x(), symbols.size.y(), 1,
        VK_IMAGE_TYPE_2D,
        VK_FORMAT_R8_UINT,
        VK_IMAGE_USAGE_SAMPLED_BIT,
        [&symbols](void* buffer) {
            uint8_t* texture_buffer = (uint8_t*)buffer;
            std::copy(symbols.pixels.begin(), symbols.pixels.end(), texture_buffer);
        }
    );
}

void Impl::OnSwapchainScopeOpen(vkrg::runtime::DispatchContext _context,
                                vktk::Swapchain const& _swapchain)
{}

void Impl::UpdateBindings(vkrg::runtime::DispatchContext _context,
                          Args const& _args)
{
    guitk::WidgetStates const* gui_record = _args.gui_record;

    _context.UpdateDynamicInvocationCount(
        _context.graph.data->parent_node,
        gui_record->frames.size());

    _context.UpdateDynamicInvocationCount(
        _context.graph["GUIFrames"],
        1);
    _context.ClearStageBarriers(
        _context.graph["GUIFrames"]);

    _context.UpdateDynamicInvocationCount(
        _context.graph["GUIGlyphs"],
        2);
    _context.ClearStageBarriers(
        _context.graph["GUIGlyphs"]);

    if (gui_record->frames.empty())
        return;

    //GUIPipelinePayload& payload = *(GUIPipelinePayload*)_rt.payload;

    gui_glyph_indices = _context.vulkan.CreateBuffer(
        sizeof(uint32_t) * 6 * gui_record->glyphs.size(),
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        [gui_record](void* buffer) {
            uint32_t* indices = (uint32_t*)buffer;

            for (uint32_t frame_index = 0u;
                 frame_index < (uint32_t)gui_record->frames.size();
                 ++frame_index)
            {
                guitk::FrameData const& frame = gui_record->frames[frame_index];
                for (uint32_t page_index = 0u;
                     page_index < guitk::GlyphPage_COUNT;
                     ++page_index)
                {
                    for (uint32_t glyph_index = 0u;
                         glyph_index < frame.glyph_range[page_index].second;
                         ++glyph_index)
                    {
                        uint32_t index_offset = glyph_index*4;
                        uint32_t index_base =
                            (glyph_index
                             + frame.glyph_range[page_index].first)*6;
                        indices[index_base + 0] = 0 + index_offset;
                        indices[index_base + 1] = 2 + index_offset;
                        indices[index_base + 2] = 1 + index_offset;
                        indices[index_base + 3] = 0 + index_offset;
                        indices[index_base + 4] = 3 + index_offset;
                        indices[index_base + 5] = 2 + index_offset;
                    }
                }
            }
        },
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    gui_glyph_vertices = _context.vulkan.CreateBuffer(
        sizeof(numtk::vec2f) * 2 * 4 * gui_record->glyphs.size(),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        [gui_record](void* buffer){
            numtk::vec2f* vertices = (numtk::vec2f*)buffer;
            numtk::vec2f* uvs = vertices + 4 * gui_record->glyphs.size();

            for (uint32_t glyph_index = 0u; glyph_index < (uint32_t)gui_record->glyphs.size(); ++glyph_index)
            {
                guitk::GlyphInfo const& glyph = gui_record->glyphs[glyph_index];
                vertices[glyph_index * 4 + 0] = glyph.bounds.min.cast<float>();
                vertices[glyph_index * 4 + 1] = numtk::vec2f{
                    (float)(glyph.bounds.min.x()+glyph.bounds.extent.x()),
                    (float)glyph.bounds.min.y()
                };
                vertices[glyph_index * 4 + 2] = glyph.bounds.max().cast<float>();
                vertices[glyph_index * 4 + 3] = numtk::vec2f{
                    (float)glyph.bounds.min.x(),
                    (float)(glyph.bounds.min.y()+glyph.bounds.extent.y())
                };

                numtk::vec2f const& min = glyph.uv.min;
                numtk::vec2f max = glyph.uv.max();
                uvs[glyph_index * 4 + 0] = numtk::vec2f{ min.x(), max.y() };
                uvs[glyph_index * 4 + 1] = max;
                uvs[glyph_index * 4 + 2] = numtk::vec2f{ max.x(), min.y() };
                uvs[glyph_index * 4 + 3] = min;
            }
        },
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    gui_frame_indices = _context.vulkan.CreateBuffer(
        sizeof(uint32_t) * 6 * 9,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        [](void* buffer) {
            static std::vector<uint32_t> const indices = {
                0, 2, 1,  1, 2, 3,
                4, 6, 5,  5, 6, 7,
                8, 10, 9,  9, 10, 11,
                12, 14, 13,  13, 14, 15,

                3, 7, 1,  1, 7, 6,
                7, 11, 5,  5, 11, 10,
                11, 15, 9,  9, 15, 14,
                15, 3, 13,  13, 3, 2,

                7, 3, 11,  11, 3, 15
            };

            uint32_t* index_data = (uint32_t*)buffer;
            std::copy(indices.begin(), indices.end(), index_data);
        },
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    gui_frames_vertices = _context.vulkan.CreateBuffer(
        sizeof(numtk::vec2f) * 16 * gui_record->frames.size(),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        [gui_record](void* buffer){
            static float const kBorderSize = 10.f;
            static numtk::vec2f const kBorderOffsets[] = {
                { kBorderSize, 0 },
                { 0, kBorderSize },
                { kBorderSize, kBorderSize },
                { -kBorderSize, kBorderSize }
            };

            numtk::vec2f* vertices = (numtk::vec2f*)buffer;
            for (uint32_t frame_index = 0u; frame_index < gui_record->frames.size(); ++frame_index)
            {
                numtk::bounds2u const& frame = gui_record->frames[frame_index].bounds;
                numtk::vec2f* base = &vertices[frame_index*16];
                base[0] = frame.min.cast<float>();
                base[1] = base[0] + kBorderOffsets[0];
                base[2] = base[0] + kBorderOffsets[1];
                base[3] = base[0] + kBorderOffsets[2];

                base[4] = numtk::vec2f{
                    (float)(frame.min.x()+frame.extent.x()),
                    (float)frame.min.y() };
                base[5] = base[4] + kBorderOffsets[1];
                base[6] = base[4] - kBorderOffsets[0];
                base[7] = base[4] + kBorderOffsets[3];

                base[8] = frame.max().cast<float>();
                base[9] = base[8] - kBorderOffsets[0];
                base[10] = base[8] - kBorderOffsets[1];
                base[11] = base[8] - kBorderOffsets[2];

                base[12] = numtk::vec2f{
                    (float)frame.min.x(),
                    (float)(frame.min.y()+frame.extent.y()) };
                base[13] = base[12] - kBorderOffsets[1];
                base[14] = base[12] + kBorderOffsets[0];
                base[15] = base[12] - kBorderOffsets[3];
            }
        },
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    gui_frame_uv = _context.vulkan.CreateBuffer(
        sizeof(numtk::vec2f) * 16,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        [](void* buffer){
            static numtk::vec2f const kBorderOffsets[] = {
                { 0.5f, 0.f },
                { 0.f, 0.5f },
                { 0.5f, 0.5f },
                { -0.5f, 0.5f }
            };

            numtk::vec2f* vertices = (numtk::vec2f*)buffer;
            vertices[0] = numtk::vec2f{ 0.f, 0.f };
            vertices[1] = vertices[0] + kBorderOffsets[0];
            vertices[2] = vertices[0] + kBorderOffsets[1];
            vertices[3] = vertices[0] + kBorderOffsets[2];

            vertices[4] = numtk::vec2f{ 1.f, 0.f };
            vertices[5] = vertices[4] + kBorderOffsets[1];
            vertices[6] = vertices[4] - kBorderOffsets[0];
            vertices[7] = vertices[4] + kBorderOffsets[3];

            vertices[8] = numtk::vec2f{ 1.f, 1.f };
            vertices[9] = vertices[8] - kBorderOffsets[0];
            vertices[10] = vertices[8] - kBorderOffsets[1];
            vertices[11] = vertices[8] - kBorderOffsets[2];

            vertices[12] = numtk::vec2f{ 0.f, 1.f };
            vertices[13] = vertices[12] - kBorderOffsets[1];
            vertices[14] = vertices[12] + kBorderOffsets[0];
            vertices[15] = vertices[12] - kBorderOffsets[3];
        },
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    for (uint32_t index = 0u; index < (uint32_t)gui_record->frames.size(); ++index)
    {
        guitk::FrameData const& frame = gui_record->frames[index];

        _context.BindGraphicsCommand(
            _context.graph["GUIFrames"],
            index,
            {
                vkrg::bind::DispatchCommand_Draw, 6*9, 1
            });


        _context.BindVertexInput(
            _context.graph["GUIFrames"],
            { .type = vkrg::layout::BindingType_VertexInput,
              .vertex_input_index = 0 },
            index,
            { gui_frames_vertices, index * sizeof(numtk::vec2f) * 16 }
        );

        _context.BindVertexInput(
            _context.graph["GUIFrames"],
            { .type = vkrg::layout::BindingType_VertexInput,
              .vertex_input_index = 1 },
            index,
            { gui_frame_uv, 0 }
        );

        _context.BindIndexInput(
            _context.graph["GUIFrames"],
            index,
            { gui_frame_indices, 0 }
        );

        dstk::Registry::Node glyphs_node =
            _context.graph["GUIGlyphs"];

        if (frame.glyph_range[guitk::GlyphPage_Font].second)
        {
            _context.BindGraphicsCommand(
                glyphs_node,
                { 0, index },
                {
                    vkrg::bind::DispatchCommand_Draw,
                    (uint32_t)frame.glyph_range[guitk::GlyphPage_Font].second*6, 1
                });

            uint32_t glyph_vertex_offset =
                frame.glyph_range[guitk::GlyphPage_Font].first*sizeof(numtk::vec2f)*4;
            _context.BindVertexInput(
                glyphs_node,
                { .type = vkrg::layout::BindingType_VertexInput,
                  .vertex_input_index = 0 },
                { 0, index },
                { gui_glyph_vertices,
                  glyph_vertex_offset }
            );

            _context.BindVertexInput(
                glyphs_node,
                { .type = vkrg::layout::BindingType_VertexInput,
                  .vertex_input_index = 1 },
                { 0, index },
                { gui_glyph_vertices,
                  (uint32_t)gui_record->glyphs.size()*sizeof(numtk::vec2f)*4
                  + glyph_vertex_offset }
            );

            _context.BindIndexInput(
                glyphs_node,
                { 0, index },
                { gui_glyph_indices,
                  (uint32_t)frame.glyph_range[guitk::GlyphPage_Font].first
                  *sizeof(uint32_t)*6 }
            );

            VkDescriptorImageInfo image_info{
                nearest_sampler,
                gui_font_texture.view,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            };

            VkWriteDescriptorSet binding =
                _context.BindShaderResource(
                    glyphs_node,
                    vkrg::layout::BindingPoint{
                        .type = vkrg::layout::BindingType_Shader,
                        .shader = { 0, 0 }
                    },
                    { 0, index },
                    vkrg::bind::ShaderResources{
                        .images = { image_info }
                    });

            vkUpdateDescriptorSets(_context.vulkan.device, 1, &binding, 0u, nullptr);
        }

        if (frame.glyph_range[guitk::GlyphPage_Symbols].second)
        {
            _context.BindGraphicsCommand(
                glyphs_node,
                { 1, index },
                {
                    vkrg::bind::DispatchCommand_Draw,
                    (uint32_t)frame.glyph_range[guitk::GlyphPage_Symbols].second*6, 1
                });

            uint32_t glyph_vertex_offset =
                frame.glyph_range[guitk::GlyphPage_Symbols].first*sizeof(numtk::vec2f)*4;
            _context.BindVertexInput(
                glyphs_node,
                { .type = vkrg::layout::BindingType_VertexInput,
                  .vertex_input_index = 0 },
                { 1, index },
                { gui_glyph_vertices,
                  glyph_vertex_offset }
            );

            _context.BindVertexInput(
                glyphs_node,
                { .type = vkrg::layout::BindingType_VertexInput,
                  .vertex_input_index = 1 },
                { 1, index },
                { gui_glyph_vertices,
                  (uint32_t)gui_record->glyphs.size()*sizeof(numtk::vec2f)*4
                  + glyph_vertex_offset }
            );

            _context.BindIndexInput(
                glyphs_node,
                { 1, index },
                { gui_glyph_indices,
                  (uint32_t)frame.glyph_range[guitk::GlyphPage_Symbols].first
                  *sizeof(uint32_t)*6 }
            );

            VkDescriptorImageInfo image_info{
                nearest_sampler,
                gui_symbols_texture.view,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            };

            VkWriteDescriptorSet binding =
                _context.BindShaderResource(
                    glyphs_node,
                    vkrg::layout::BindingPoint{
                        .type = vkrg::layout::BindingType_Shader,
                        .shader = { 0, 0 }
                    },
                    { 1, index },
                    vkrg::bind::ShaderResources{
                        .images = { image_info }
                    });

            vkUpdateDescriptorSets(_context.vulkan.device, 1, &binding, 0u, nullptr);
        }
    }

    _context.BindBarrier(
        _context.graph["GUIFrames"], {
            .type = vkrg::bind::BufferResource,
            .stage = VK_PIPELINE_STAGE_TRANSFER_BIT,
            .access = VK_ACCESS_TRANSFER_WRITE_BIT,
            .buffer = gui_frames_vertices
        });

    _context.BindBarrier(
        _context.graph["GUIFrames"], {
            .type = vkrg::bind::BufferResource,
            .stage = VK_PIPELINE_STAGE_TRANSFER_BIT,
            .access = VK_ACCESS_TRANSFER_WRITE_BIT,
            .buffer = gui_frame_indices
        });

    _context.BindBarrier(
        _context.graph["GUIFrames"], {
            .type = vkrg::bind::BufferResource,
            .stage = VK_PIPELINE_STAGE_TRANSFER_BIT,
            .access = VK_ACCESS_TRANSFER_WRITE_BIT,
            .buffer = gui_frame_uv
        });

    _context.BindBarrier(
        _context.graph["GUIGlyphs"], {
            .type = vkrg::bind::BufferResource,
            .stage = VK_PIPELINE_STAGE_TRANSFER_BIT,
            .access = VK_ACCESS_TRANSFER_WRITE_BIT,
            .buffer = gui_glyph_vertices
        });

    _context.BindBarrier(
        _context.graph["GUIGlyphs"], {
            .type = vkrg::bind::BufferResource,
            .stage = VK_PIPELINE_STAGE_TRANSFER_BIT,
            .access = VK_ACCESS_TRANSFER_WRITE_BIT,
            .buffer = gui_glyph_indices
        });

    for (uint32_t index = 0u;
         index < (uint32_t)gui_record->frames.size();
         ++index)
    {
        numtk::vec2f* frame_constants =
            (numtk::vec2f*)_context.BindPushConstants(
                _context.graph["GUIFrames"],
                {
                    .type = vkrg::layout::BindingType_PushConstantRange,
                    .push_constant_range = 0
                }, index);
        frame_constants[0] = _args.framebuffer_size;

        numtk::vec2f* font_constants =
            (numtk::vec2f*)_context.BindPushConstants(
                _context.graph["GUIGlyphs"],
                {
                    .type = vkrg::layout::BindingType_PushConstantRange,
                    .push_constant_range = 0
                }, { 0, index });
        font_constants[0] = _args.framebuffer_size;

        numtk::vec2f* symbols_constants =
            (numtk::vec2f*)_context.BindPushConstants(
                _context.graph["GUIGlyphs"],
                {
                    .type = vkrg::layout::BindingType_PushConstantRange,
                    .push_constant_range = 0
                }, { 1, index });
        symbols_constants[0] = _args.framebuffer_size;
    }
}

void Impl::PostSubmit(vkrg::runtime::DispatchContext _context,
                      VkFence _fence)
{
    _context.vulkan.ScheduleRoutine(
        _fence,
        [index_buffer = gui_frame_indices
         , vertex_buffer = gui_frames_vertices
         , uv_buffer = gui_frame_uv
         , glyph_indices = gui_glyph_indices
         , glyph_vertices = gui_glyph_vertices](vktk::Context& _vulkan) {
            _vulkan.DestroyBuffer(index_buffer);
            _vulkan.DestroyBuffer(vertex_buffer);
            _vulkan.DestroyBuffer(uv_buffer);
            _vulkan.DestroyBuffer(glyph_indices);
            _vulkan.DestroyBuffer(glyph_vertices);
        });
}

vkrg::layout::DispatchStage FramesDispatchStageInfo()
{
    vkrg::layout::DispatchStage output = {};
    output.name = "guiframes";

    output.shaders = std::vector<vkrg::layout::ShaderStage>{
        { VK_SHADER_STAGE_VERTEX_BIT,
          vktk::Context::CompileShader_GLSLtoSPIRV(
              VK_SHADER_STAGE_VERTEX_BIT, "guiframes_vertex",
              { "#version 450",
                "layout(location = 0) in vec2 iVS_point;"
                "layout(location = 1) in vec2 iVS_uv;"
                "layout(location = 0) out vec2 iFS_point;"
                "layout(push_constant, std430) uniform PushConstants { vec2 viewport_size; };"
                "void main() {"
                "gl_Position = vec4((iVS_point / viewport_size) * vec2(2.0) - vec2(1.0), 0.0, 1.0);"
                "iFS_point = iVS_uv;"
                "}"
              }
          )
        },

        { VK_SHADER_STAGE_FRAGMENT_BIT,
          vktk::Context::CompileShader_GLSLtoSPIRV(
              VK_SHADER_STAGE_FRAGMENT_BIT, "guiframes_fragment",
              { "#version 450",
                "layout(location = 0) in vec2 iFS_point;"
                "layout(location = 0) out vec4 color;"
                "layout(push_constant, std430) uniform PushConstants { vec2 viewport_size; };"

                "void main() {"
                "vec2 frame_coords = iFS_point - vec2(0.5);"
                "float border_distance = length(frame_coords);"
                "float border_color = min(smoothstep(0.0, 0.25, border_distance), smoothstep(0.5, 0.25, border_distance));"
                "color = vec4(border_color.xxx, 1.0);"
                "}"
              }
          )
        }
    };

    output.invocation_count = 0;

    output.push_constants = std::vector<vkrg::layout::PushConstantRange>{
        { VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(numtk::vec2f) }
    };

    output.graphics.state_overrides = std::vector<vktk::Context::StateOverride>{
        { vktk::StateOverrideTag::DepthTestEnable, VK_FALSE },
        { vktk::StateOverrideTag::DepthWriteEnable, VK_FALSE }
    };
    output.graphics.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    output.graphics.input_layout = std::vector<vktk::Context::VertexInput>{
        { 0, 0, VK_VERTEX_INPUT_RATE_VERTEX, VK_FORMAT_R32G32_SFLOAT,
          0, sizeof(numtk::vec2f) },
        { 1, 1, VK_VERTEX_INPUT_RATE_VERTEX, VK_FORMAT_R32G32_SFLOAT,
          0, sizeof(numtk::vec2f) }
    };

    return output;
}

vkrg::layout::DispatchStage GlyphsDispatchStageInfo()
{
    vkrg::layout::DispatchStage output = {};
    output.name = "guiglyphs";

    output.shaders = std::vector<vkrg::layout::ShaderStage>{
        { VK_SHADER_STAGE_VERTEX_BIT,
          vktk::Context::CompileShader_GLSLtoSPIRV(
              VK_SHADER_STAGE_VERTEX_BIT, "guiglyphs_vertex",
              { "#version 450",
                "layout(location = 0) in vec2 iVS_point;"
                "layout(location = 1) in vec2 iVS_uv;"
                "layout(location = 0) out vec2 iFS_uv;"
                "layout(push_constant, std430) uniform PushConstants { vec2 viewport_size; };"
                "void main() {"
                "gl_Position = vec4((iVS_point / viewport_size) * vec2(2.0) - vec2(1.0), 0.0, 1.0);"
                "iFS_uv = iVS_uv;"
                "}"
              }
          )
        },

        { VK_SHADER_STAGE_FRAGMENT_BIT,
          vktk::Context::CompileShader_GLSLtoSPIRV(
              VK_SHADER_STAGE_FRAGMENT_BIT, "guiglyphs_fragment",
              { "#version 450",
                "layout(location = 0) in vec2 iFS_uv;"
                "layout(location = 0) out vec4 color;"
                "layout(set = 0, binding = 0) uniform usampler2D glyph_atlas;"
                "void main() {"
                "uint glyph_pixel = texture(glyph_atlas, iFS_uv).x;"
                "if (glyph_pixel == 0) discard;"
                "color = vec4(1.0);"
                "}"
              }
          )
        }
    };

    output.invocation_count = 0;
    output.push_constants = std::vector<vkrg::layout::PushConstantRange>{
        { VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(numtk::vec2f) }
    };
    output.binding_groups.push_back(vkrg::layout::StageBindingGroup{
        {
            { VK_SHADER_STAGE_FRAGMENT_BIT, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
              0, 1 }
        }
    });

    output.graphics.state_overrides = std::vector<vktk::Context::StateOverride>{};
    output.graphics.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    output.graphics.input_layout = std::vector<vktk::Context::VertexInput>{
        { 0, 0, VK_VERTEX_INPUT_RATE_VERTEX, VK_FORMAT_R32G32_SFLOAT,
          0, sizeof(numtk::vec2f) },
        { 1, 1, VK_VERTEX_INPUT_RATE_VERTEX, VK_FORMAT_R32G32_SFLOAT,
          0, sizeof(numtk::vec2f) }
    };

    return output;
}

vkrg::layout::DispatchGraph RenderGraph(dstk::Registry& _registry)
{
    vkrg::layout::DispatchGraph output{ &_registry };
    _registry.BindComponent(output["GUIFrames"], FramesDispatchStageInfo());
    _registry.BindComponent(output["GUIGlyphs"], GlyphsDispatchStageInfo());
    output.MakeEdge("GUIFrames", "GUIGlyphs");
    return output;
}

} // namespace gui_runtime
