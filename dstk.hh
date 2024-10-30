#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

namespace dstk
{

struct FreeList
{
    using Index = uint64_t;
    struct Range { Index begin, end; };

    FreeList() : indices{ Range{ 0, UINT64_MAX } } {}
    FreeList(FreeList const&) = default;
    FreeList(FreeList&&) = default;
    FreeList& operator=(FreeList const&) = default;
    FreeList& operator=(FreeList&&) = default;

    Index Reserve() {
        Index result = indices.front().begin;
        ++indices.front().begin;
        if (indices.front().begin == indices.front().end)
            indices.erase(indices.begin());
        return result;
    }

    void Release(Index handle) {
        auto range_iterator = std::upper_bound(
            indices.begin(), indices.end(), handle,
            [](Index handle, Range const& range) { return handle < range.begin; });

        bool location_found = false;
        bool has_prev = (range_iterator != indices.begin());
        bool has_value = (range_iterator != indices.end());

        if (has_prev && handle < std::prev(range_iterator)->end)
            return;

        if (has_prev && handle == std::prev(range_iterator)->end)
        {
            std::prev(range_iterator)->end++;
            location_found = true;
        }
        else if (has_value && handle == range_iterator->begin - 1)
        {
            range_iterator->begin--;
            location_found = true;
        }

        if (!location_found)
            indices.insert(range_iterator, { handle, handle+1 });

        if (has_prev && has_value && std::prev(range_iterator)->end == range_iterator->begin)
        {
            std::prev(range_iterator)->end = range_iterator->end;
            indices.erase(range_iterator);
        }
    }

    uint64_t RequiredSize() const
    {
        return indices.back().begin;
    }

    std::vector<Range> indices;

#if 0
    static void UnitTests() {
        FreeList free_list{};
        {
            for (uint32_t index = 0; index < 50; ++index)
                free_list.Reserve();
            uint64_t size = free_list.RequiredSize();
        }

        {
            for (uint32_t index = 0; index < 25; ++index)
                free_list.Release((FreeList::Index)index * 2);
            uint64_t size = free_list.RequiredSize();
        }

        {
            std::vector<uint64_t> indices{};
            for (uint32_t index = 0; index < 10; ++index)
                indices.push_back(free_list.Reserve());
            uint64_t size = free_list.RequiredSize();
        }

        {
            for (uint32_t index = 0; index < 15; ++index)
                free_list.Release((FreeList::Index)index);
            uint64_t size = free_list.RequiredSize();
        }

        {
            for (uint32_t index = 0; index < 50; ++index)
                free_list.Release((FreeList::Index)index);
            uint64_t size = free_list.RequiredSize();
        }
    }
#endif
};

struct Registry
{
};

}
