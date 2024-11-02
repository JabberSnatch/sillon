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

template <typename Key, typename Value>
struct OrderedVector
{
    template <typename OrderPredicate>
    OrderedVector(OrderPredicate _order_predicate)
        : order_predicate{ _order_predicate }
    {}

    std::vector<Value>::iterator insert(Key const& k, Value const& v)
    {
        auto key_position = std::lower_bound(keys.begin(), keys.end(), k, order_predicate);
        auto item_position = std::next(items.begin(), std::distance(keys.begin(), key_position));
        if (key_position != keys.end() && *key_position == k)
        {
            *item_position = v;
            return item_position;
        }
        else
        {
            keys.insert(key_position, k);
            return items.insert(item_position, v);
        }
    }

    void erase(Key const& k)
    {
        auto key_position = std::lower_bound(keys.begin(), keys.end(), k, order_predicate);
        auto item_position = std::next(items.begin(), std::distance(keys.begin(), key_position));
        if (key_position != keys.end() && *key_position == k)
        {
            keys.erase(key_position);
            items.erase(item_position);
        }
    }

    Value const& operator[](Key const& k) const
    {
        auto key_position = std::lower_bound(keys.begin(), keys.end(), k, order_predicate);
        if (*key_position == k)
            return *std::next(items.begin(), std::distance(keys.begin(), key_position));
        else
            throw std::out_of_range{"Invalid key"};
    }

    Value& operator[](Key const& k)
    {
        return const_cast<Value&>(
            const_cast<OrderedVector<Key, Value> const*>(this)->operator[](k)
        );
    }

    std::function<bool(Key const&, Key const&)> order_predicate;
    std::vector<Value> items{};
    std::vector<Key> keys{};
};

struct Registry
{
};

}
