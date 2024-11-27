#pragma once

#include <algorithm>
#include <cstdint>
#include <memory>
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
};

struct BlockVector
{
    BlockVector(uint64_t _object_size, uint64_t _block_size)
        : object_size{ _object_size }
        , block_size{ _block_size }
    {}

    void Expand(uint64_t object_count) {
        if (object_count <= Capacity())
            return;

        uint64_t delta = object_count - Capacity();
        uint64_t block_count = (delta + object_size-1) / object_size;
        for (uint64_t index = 0; index < block_count; ++index)
            blocks.emplace_back(new uint8_t[object_size * block_size]);
    }

    void* operator[](uint64_t index) const {
        if (index >= Capacity())
            return nullptr;

        uint64_t block_index = index / block_size;
        uint64_t object_index = index - (block_index * block_size);
        return (void*)(blocks[block_index].get() + object_index * object_size);
    }

    uint64_t Capacity() const { return block_size * blocks.size(); }

    uint64_t object_size; // bytes
    uint64_t block_size; // object count
    std::vector<std::unique_ptr<uint8_t[]>> blocks{};
};

template <typename T>
struct ObjectPool
{
    using Handle = uint64_t;

    void Expand(uint64_t required_size)
    {
        items.Expand(required_size);
    }

    Handle Reserve() {
        Handle handle = free_list.Reserve();
        items.Expand(free_list.RequiredSize());
        new (items[handle]) T{};
        return handle;
    }

    T* operator[](Handle handle) const {
        return (T*)items[handle];
    }

    void Release(Handle handle) {
        ((T*)items[handle])->~T();
        free_list.Release(handle);
    }

    BlockVector items{ sizeof(T), 256ull };
    FreeList free_list{};
};

template <typename Key, typename Value>
struct OrderedVector
{
    OrderedVector() = default;
    OrderedVector(OrderedVector const&) = default;
    OrderedVector(OrderedVector&&) = default;
    OrderedVector& operator=(OrderedVector const&) = default;
    OrderedVector& operator=(OrderedVector&&) = default;

    std::vector<Value>::iterator insert(Key const& k, Value const& v)
    {
        auto key_position = std::lower_bound(keys.begin(), keys.end(), k);
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
        auto key_position = std::lower_bound(keys.begin(), keys.end(), k);
        auto item_position = std::next(items.begin(), std::distance(keys.begin(), key_position));
        if (key_position != keys.end() && *key_position == k)
        {
            keys.erase(key_position);
            items.erase(item_position);
        }
    }

    bool contains(Key const& k) const {
        auto key_position = std::lower_bound(keys.begin(), keys.end(), k);
        return key_position != keys.end() && *key_position == k;
    }

    Value const& operator[](Key const& k) const
    {
        auto key_position = std::lower_bound(keys.begin(), keys.end(), k);
        if (key_position != keys.end() && *key_position == k)
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

    std::vector<Value> items{};
    std::vector<Key> keys{};
};

}
