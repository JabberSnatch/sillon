#pragma once

#include "numtk.hh"
#include "dstk.hh"

namespace voxtk
{

struct VoxelMask
{
    static constexpr int32_t kSize = 8;

    bool Test(numtk::vec3u const& point) const {
        numtk::vec3u const pack_point = point / 4u;
        numtk::vec3u const bit_point = point % 4u;
        uint32_t const pack_index = pack_point.x() + pack_point.y() * 2u + pack_point.z() * 4u;
        uint32_t const bit_index = bit_point.x() + bit_point.y() * 4u + bit_point.z() * 16u;
        return !!((bits[pack_index] >> bit_index) & 1);
    }

    void Set(numtk::vec3u const& point, bool v) {
        numtk::vec3u const pack_point = point / 4u;
        numtk::vec3u const bit_point = point % 4u;
        uint32_t const pack_index = pack_point.x() + pack_point.y() * 2u + pack_point.z() * 4u;
        uint32_t const bit_index = bit_point.x() + bit_point.y() * 4u + bit_point.z() * 16u;
        if (v)
            bits[pack_index] |= (1ull << bit_index);
        else
            bits[pack_index] &= ~(1ull << bit_index);
    }

    uint64_t Kernel4(numtk::vec3u const& base) const;

    bool Full() const {
        return
            bits[0] == 0xffffffffffffffffull
            && bits[1] == 0xffffffffffffffffull
            && bits[2] == 0xffffffffffffffffull
            && bits[3] == 0xffffffffffffffffull
            && bits[4] == 0xffffffffffffffffull
            && bits[5] == 0xffffffffffffffffull
            && bits[6] == 0xffffffffffffffffull
            && bits[7] == 0xffffffffffffffffull;
    }

    bool Empty() const {
        return
            !bits[0]
            && !bits[1]
            && !bits[2]
            && !bits[3]
            && !bits[4]
            && !bits[5]
            && !bits[6]
            && !bits[7];
    }

    uint64_t bits[8];
};

struct Region
{
    struct Node;

    Region(numtk::vec3u _size)
        : level_count{ std::max(
            numtk::ilogN<VoxelMask::kSize>(
                std::max(std::max(_size.x(), _size.y()), _size.z())
            ), 1u) }
        , size{ _size }
    {}

    bool Test(numtk::vec3u const& point) const {
        if (point.x() >= size.x()
            || point.y() >= size.y()
            || point.z() >= size.z())
            return false;

        Node* current_node = root;
        while (current_node)
        {
            numtk::vec3u local_point = point / (1u << (3 * current_node->depth));
            if (current_node->child_mask.Test(local_point))
                current_node = current_node->children[local_point];
            else
                return current_node->data_mask.Test(local_point);
        }

        return false;
    }

    void Set(numtk::vec3u const& point, bool v) {
        if (point.x() >= size.x()
            || point.y() >= size.y()
            || point.z() >= size.z())
            return;

        if (!root)
        {
            root = node_pool[node_pool.Reserve()];
            root->parent = nullptr;
            root->location = numtk::vec3u{ 0, 0, 0 };
            root->depth = level_count - 1;
        }

        Node* current_node = root;
        while (current_node)
        {
            numtk::vec3u local_point =
                (point
                 / (1u << (3 * current_node->depth)))
                % VoxelMask::kSize;

            if (current_node->depth == 0)
            {
                current_node->data_mask.Set(local_point, v);
                break;
            }
            else if (current_node->child_mask.Test(local_point))
                current_node = current_node->children[local_point];
            else if (current_node->data_mask.Test(local_point) != v)
            {
                auto next_node =
                    current_node->children.insert(local_point, node_pool[node_pool.Reserve()]);
                (*next_node)->parent = current_node;
                (*next_node)->location = local_point;
                (*next_node)->depth = current_node->depth - 1;
                current_node->child_mask.Set(local_point, true);
                current_node = *next_node;
            }
        }
    }

    struct Node {
        Node* parent;
        numtk::vec3u location;
        uint32_t depth;

        VoxelMask child_mask{};
        VoxelMask data_mask{};
        dstk::OrderedVector<numtk::vec3u, Node*> children{
            [](numtk::vec3u const& _lhs, numtk::vec3u const& _rhs) {
                return _lhs.x() < _rhs.x()
                    || (_lhs.x() == _rhs.x()
                        && (_lhs.y() < _rhs.y()
                            || (_lhs.y() == _rhs.y() && _lhs.z() < _rhs.z())));
            }
        };
    };

    Node* root = nullptr;
    dstk::ObjectPool<Node> node_pool{};
    uint32_t level_count;
    numtk::vec3u size;
};

}
