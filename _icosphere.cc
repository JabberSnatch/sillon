#include "_icosphere.hh"

#include "loader/iotk.hpp"
#include "numtk.hh"
#include "vktk.hh"

#include <iostream>

namespace {

struct state_t
{
    VkPipelineLayout pipeline_layout;
    vktk::Buffer index_buffer;
    vktk::Buffer vertex_buffer;
    void* dummy;
};

void ConstantResources(void* _pstate,
                       vktk::Context* _vulkan)
{
    state_t* _state = (state_t*)_pstate;

    VkPushConstantRange push_constants{};
    push_constants.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    push_constants.offset = 0;
    push_constants.size = sizeof(numtk::Mat4_t);

    VkPipelineLayoutCreateInfo pipeline_create{};
    pipeline_create.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_create.pNext = nullptr;
    pipeline_create.flags = 0u;
    pipeline_create.setLayoutCount = 0u;
    pipeline_create.pSetLayouts = nullptr;
    pipeline_create.pushConstantRangeCount = 1u;
    pipeline_create.pPushConstantRanges = &push_constants;
    CHECKCALL(vkCreatePipelineLayout, _vulkan->device, &pipeline_create, nullptr,
              &_state->pipeline_layout);

    vktk::Buffer staging_buffer = _vulkan->CreateBuffer(
        16 * 1024 * 1024,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
        | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    _state->index_buffer = _vulkan->CreateBuffer(
        20 * sizeof(uint32_t) * 3,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT
        | VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

    _state->vertex_buffer = _vulkan->CreateBuffer(
        12 * sizeof(float) * 3,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT
        | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

    void* staging_area = nullptr;
    CHECKCALL(vkMapMemory, _vulkan->device, staging_buffer.memory, 0, VK_WHOLE_SIZE, 0,
              &staging_area);

    numtk::vec3f* vertices = (numtk::vec3f*)staging_area;
    numtk::vec3u* triangles = (numtk::vec3u*)(vertices+12);
    {
        static const auto spherical_to_cartesian = [](numtk::vec3f const& v){
            // v = { lat, long, elevation }
            // { 0, 0, 1 } -> { 0, 0, 1 }
            // { 90, 0, 1 } -> { 0, 1, 0 }
            // { -90, 0, 1 } -> { 0, -1, 0 }
            return v.z() * numtk::vec3f{
                std::cos(v.y())*std::cos(v.x()),
                std::sin(v.x()),
                std::sin(v.y())*std::cos(v.x())
            };
        };

        static constexpr float kPoleLat = 90.f*numtk::kDegrees;
        static constexpr float kCommonLat = 26.57f*numtk::kDegrees;
        static constexpr float kLongIncrement = 36.f*numtk::kDegrees;
        vertices[0] = spherical_to_cartesian({ kPoleLat, 0.f, 1.f });
        vertices[1] = spherical_to_cartesian({ kCommonLat, kLongIncrement*0.f, 1.f });
        vertices[2] = spherical_to_cartesian({ -kCommonLat, kLongIncrement*1.f, 1.f });
        vertices[3] = spherical_to_cartesian({ kCommonLat, kLongIncrement*2.f, 1.f });
        vertices[4] = spherical_to_cartesian({ -kCommonLat, kLongIncrement*3.f, 1.f });
        vertices[5] = spherical_to_cartesian({ kCommonLat, kLongIncrement*4.f, 1.f });
        vertices[6] = spherical_to_cartesian({ -kCommonLat, kLongIncrement*5.f, 1.f });
        vertices[7] = spherical_to_cartesian({ kCommonLat, kLongIncrement*6.f, 1.f });
        vertices[8] = spherical_to_cartesian({ -kCommonLat, kLongIncrement*7.f, 1.f });
        vertices[9] = spherical_to_cartesian({ kCommonLat, kLongIncrement*8.f, 1.f });
        vertices[10] = spherical_to_cartesian({ -kCommonLat, kLongIncrement*9.f, 1.f });
        vertices[11] = spherical_to_cartesian({ -kPoleLat, 0.f, 1.f });
    }

    {
        triangles[0] = { 0, 1, 3 };
        triangles[1] = { 0, 3, 5 };
        triangles[2] = { 0, 5, 7 };
        triangles[3] = { 0, 7, 9 };
        triangles[4] = { 0, 9, 1 };

        triangles[5] = { 11, 10, 8 };
        triangles[6] = { 11, 8, 6 };
        triangles[7] = { 11, 6, 4 };
        triangles[8] = { 11, 4, 2 };
        triangles[9] = { 11, 2, 10 };

        triangles[10] = { 1, 2, 3 };
        triangles[11] = { 3, 4, 5 };
        triangles[12] = { 5, 6, 7 };
        triangles[13] = { 7, 8, 9 };
        triangles[14] = { 9, 10, 1 };

        triangles[15] = { 10, 9, 8 };
        triangles[16] = { 8, 7, 6 };
        triangles[17] = { 6, 5, 4 };
        triangles[18] = { 4, 3, 2 };
        triangles[19] = { 2, 1, 10 };
    }
}

void RenderResources(void* _pstate,
                     vktk::Context* _vulkan,
                     vktk::Swapchain const& _swapchain)
{
}

void Update(void* _pstate,
            vktk::Context* _vulkan,
            iotk::input_t const* _input,
            iotk::input_t const* _lastInput)
{
}

void Draw(void* _pstate,
          vktk::Context* _vulkan,
          vktk::Swapchain const& _swapchain,
          VkSemaphore _acquire_semaphore)
{
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

void Shutdown(void* _pstate, vktk::Context* _vulkan)
{
}


}

namespace Icosphere
{
Interface::Interface() :
    ConstantResources{ ::ConstantResources },
    RenderResources{ ::RenderResources },
    Update{ ::Update },
    Draw{ ::Draw },
    Shutdown{ ::Shutdown },
    state{ new state_t{} }
{}

Interface::~Interface()
{
    delete (state_t*)state;
}

}
