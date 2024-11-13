#pragma once

#include "numtk.hh"
#include "dstk.hh"

namespace voxtk
{

struct VoxelMask
{
    static constexpr int32_t kSize = 8;
    static constexpr int32_t kLogSize = 3;
    static constexpr int32_t kVolume = kSize*kSize*kSize;

    void Clear() {
        bits[0] = 0ull;
        bits[1] = 0ull;
        bits[2] = 0ull;
        bits[3] = 0ull;
        bits[4] = 0ull;
        bits[5] = 0ull;
        bits[6] = 0ull;
        bits[7] = 0ull;
    }

    void BitReverse() {
        bits[0] = numtk::BitReverse(bits[0]);
        bits[1] = numtk::BitReverse(bits[1]);
        bits[2] = numtk::BitReverse(bits[2]);
        bits[3] = numtk::BitReverse(bits[3]);
        bits[4] = numtk::BitReverse(bits[4]);
        bits[5] = numtk::BitReverse(bits[5]);
        bits[6] = numtk::BitReverse(bits[6]);
        bits[7] = numtk::BitReverse(bits[7]);
        std::swap(bits[0], bits[7]);
        std::swap(bits[1], bits[6]);
        std::swap(bits[2], bits[5]);
        std::swap(bits[3], bits[4]);
    }

    VoxelMask BitwiseAnd(VoxelMask const& o) const {
        VoxelMask output{};
        output.bits[0] = bits[0] & o.bits[0];
        output.bits[1] = bits[1] & o.bits[1];
        output.bits[2] = bits[2] & o.bits[2];
        output.bits[3] = bits[3] & o.bits[3];
        output.bits[4] = bits[4] & o.bits[4];
        output.bits[5] = bits[5] & o.bits[5];
        output.bits[6] = bits[6] & o.bits[6];
        output.bits[7] = bits[7] & o.bits[7];
        return output;
    }

    VoxelMask BitwiseOr(VoxelMask const& o) const {
        VoxelMask output{};
        output.bits[0] = bits[0] | o.bits[0];
        output.bits[1] = bits[1] | o.bits[1];
        output.bits[2] = bits[2] | o.bits[2];
        output.bits[3] = bits[3] | o.bits[3];
        output.bits[4] = bits[4] | o.bits[4];
        output.bits[5] = bits[5] | o.bits[5];
        output.bits[6] = bits[6] | o.bits[6];
        output.bits[7] = bits[7] | o.bits[7];
        return output;
    }

    VoxelMask BitwiseNot() const {
        VoxelMask output{};
        output.bits[0] = ~bits[0];
        output.bits[1] = ~bits[1];
        output.bits[2] = ~bits[2];
        output.bits[3] = ~bits[3];
        output.bits[4] = ~bits[4];
        output.bits[5] = ~bits[5];
        output.bits[6] = ~bits[6];
        output.bits[7] = ~bits[7];
        return output;
    }

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

    static inline uint64_t PackFillMask(numtk::vec3u const& begin) {
        uint64_t mask = 0ull;

        if (begin.x() == 0 && begin.y() == 0 && begin.z() == 0)
            mask = ~0ull;
        else if (begin.x() == 0 && begin.y() == 0)
            mask = ~((1ull << begin.z()*16) - 1ull);
        else if (begin.x() == 0)
        {
            uint64_t plane = ~((1ull << begin.y()*4) - 1ull) & 0xffffull;
            for (uint32_t z = begin.z(); z < 4; ++z)
                mask |= plane << (z*16);
        }
        else
        {
            uint64_t row = ~((1ull << begin.x()) - 1ull) & 0xfull;
            for (uint32_t z = begin.z(); z < 4; ++z)
                for (uint32_t y = begin.y(); y < 4; ++y)
                    mask |= row << (y*4 + z*16);
        }

        return mask;
    }

    void FillAbove(numtk::vec3u const& begin) {
        if (begin.x() > 7u || begin.y() > 7u || begin.z() > 7u)
            return;

        numtk::vec3u const pack_begin = begin / 4u;
        numtk::vec3u const bits_begin = begin % 4u;

        numtk::vec3u quadrant0 = pack_begin;
        bits[quadrant0.x() + quadrant0.y()*2 + quadrant0.z()*4] = PackFillMask(bits_begin);

        if (quadrant0.x() == 0)
        {
            numtk::vec3u const quadrant1{ 1u, quadrant0.y(), quadrant0.z() };
            bits[quadrant1.x() + quadrant1.y()*2 + quadrant1.z()*4] =
                PackFillMask(numtk::vec3u{ 0, bits_begin.y(), bits_begin.z() });
        }

        if (quadrant0.y() == 0)
        {
            numtk::vec3u const quadrant2{ quadrant0.x(), 1u, quadrant0.z() };
            bits[quadrant2.x() + quadrant2.y()*2 + quadrant2.z()*4] =
                PackFillMask(numtk::vec3u{ bits_begin.x(), 0, bits_begin.z() });
        }

        if (quadrant0.z() == 0)
        {
            numtk::vec3u const quadrant3{ quadrant0.x(), quadrant0.y(), 1u };
            bits[quadrant3.x() + quadrant3.y()*2 + quadrant3.z()*4] =
                PackFillMask(numtk::vec3u{ bits_begin.x(), bits_begin.y(), 0 });
        }

        if (quadrant0.x() == 0 && quadrant0.y() == 0)
        {
            numtk::vec3u const quadrant4{ 1u, 1u, quadrant0.z() };
            bits[quadrant4.x() + quadrant4.y()*2 + quadrant4.z()*4] =
                PackFillMask(numtk::vec3u{ 0, 0, bits_begin.z() });
        }

        if (quadrant0.y() == 0 && quadrant0.z() == 0)
        {
            numtk::vec3u const quadrant5{ quadrant0.x(), 1u, 1u };
            bits[quadrant5.x() + quadrant5.y()*2 + quadrant5.z()*4] =
                PackFillMask(numtk::vec3u{ bits_begin.x(), 0, 0 });
        }

        if (quadrant0.x() == 0 && quadrant0.z() == 0)
        {
            numtk::vec3u const quadrant6{ 1u, quadrant0.y(), 1u };
            bits[quadrant6.x() + quadrant6.y()*2 + quadrant6.z()*4] =
                PackFillMask(numtk::vec3u{ 0, bits_begin.y(), 0 });
        }

        if (quadrant0.x() == 0 && quadrant0.y() == 0 && quadrant0.z() == 0)
            bits[7] = ~0ull;
    }

    void FillBelow(numtk::vec3u const& end) {
        if (end.x() == 0 || end.y() == 0 || end.z() == 0)
            return;

        FillAbove(numtk::vec3u{ 8u, 8u, 8u, } - end);
        BitReverse();
    }

    void FillArea(numtk::vec3u const& begin, numtk::vec3u const& end) {
        VoxelMask above{};
        above.FillAbove(begin);
        FillBelow(end);
        *this = BitwiseAnd(above);
    }

    void Set(numtk::vec3u const& begin, numtk::vec3u const& end, bool v) {
        VoxelMask mask{};
        mask.FillArea(begin, end);
        if (v)
            *this = BitwiseOr(mask);
        else
        {
            mask = mask.BitwiseNot();
            *this = BitwiseAnd(mask);
        }
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

template <typename DataType>
struct Region
{
    struct Node;

    Region(numtk::vec3u _size)
        : level_count{ 0u }
        , size{ _size }
    {
        uint32_t max_size = std::max(std::max(_size.x(), _size.y()), _size.z());
        uint32_t log_size = numtk::ilogN<VoxelMask::kSize>(max_size);
        if (1u << (VoxelMask::kLogSize * log_size) < max_size)
            ++log_size;
        level_count = std::max(log_size, 1u);
    }

    DataType const& operator[](uint32_t const& index) const {
        numtk::vec3u point{};
        point.x() = index % size.x();
        point.y() = (index / size.x()) % size.y();
        point.z() = index / (size.x() * size.y());
        return (*this)[point];
    }

    DataType const& operator[](numtk::vec3u const& point) const {
        static DataType const fallback{};
        if (point.x() >= size.x()
            || point.y() >= size.y()
            || point.z() >= size.z())
            return fallback;

        Node* current_node = root;
        while (current_node)
        {
            numtk::vec3u local_point =
                point / (1u << (3 * current_node->depth))
                - current_node->Begin();
            if (current_node->child_mask.Test(local_point))
                current_node = current_node->children[local_point];
            else
                return (*current_node)[local_point];
        }

        return fallback;
    }

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
            numtk::vec3u local_point = (point / (1u<<(3*current_node->depth)))
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

    void Set(numtk::vec3u const& point, DataType const& value) {
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
            numtk::vec3u local_point = (point / (1u<<(3*current_node->depth)))
                % VoxelMask::kSize;

            if (current_node->depth == 0)
            {
                current_node->data_mask.Set(local_point, true);
                current_node->SetData(local_point, value);
                break;
            }
            else if (current_node->child_mask.Test(local_point))
                current_node = current_node->children[local_point];
            else
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

    void Set(numtk::vec3u const& begin, numtk::vec3u const& end, DataType const& value) {
        if (begin.x() >= size.x()
            || begin.y() >= size.y()
            || begin.z() >= size.z())
            return;

        if (!root)
        {
            root = node_pool[node_pool.Reserve()];
            root->parent = nullptr;
            root->location = numtk::vec3u{ 0, 0, 0 };
            root->depth = level_count - 1;
        }

        std::vector<Node*> node_queue{ root };
        while (!node_queue.empty())
        {
            Node* current_node = node_queue.back();
            node_queue.pop_back();

            if (current_node->depth == 0)
            {
                numtk::vec3u const data_begin =
                    numtk::max(begin, current_node->Begin()) - current_node->Begin();
                numtk::vec3u const data_end =
                    numtk::min(end, current_node->End()) - current_node->Begin();

                current_node->data_mask.Set(data_begin, data_end, true);
                current_node->SetData(data_begin, data_end, value);
            }
            else
            {
                numtk::vec3u const children_begin =
                    numtk::max(begin / (1u<<(3*(current_node->depth))),
                               current_node->Begin());
                numtk::vec3u const children_end =
                    numtk::min((end / (1u<<(3*(current_node->depth)))) + numtk::vec3u::Constant(1u),
                               current_node->End());

                for (uint32_t x = children_begin.x(); x < children_end.x(); ++x)
                    for (uint32_t y = children_begin.y(); y < children_end.y(); ++y)
                        for (uint32_t z = children_begin.z(); z < children_end.z(); ++z)
                        {
                            numtk::vec3u const child{ x, y, z };
                            if (!current_node->children.contains(child))
                            {
                                auto child_node = current_node->children.insert(
                                    child, node_pool[node_pool.Reserve()]);
                                (*child_node)->parent = current_node;
                                (*child_node)->location = child;
                                (*child_node)->depth = current_node->depth - 1;
                                current_node->child_mask.Set(child, true);
                            }
                            node_queue.push_back(current_node->children[child]);
                        }
            }
        }
    }

    struct Node {
        Node* parent;
        numtk::vec3u location;
        uint32_t depth;

        numtk::vec3u Begin() const { return location * (1u << (3*(depth+1))); }
        numtk::vec3u End() const { return Begin() + numtk::vec3u::Constant(8u); }

        void SetData(numtk::vec3u const& point, DataType const& value) {
            if (data.empty()) data.resize(VoxelMask::kVolume);
            uint32_t const index =
                point.x()
                + point.y()*VoxelMask::kSize
                + point.z()*VoxelMask::kSize*VoxelMask::kSize;
            data[index] = value;
        }

        void SetData(numtk::vec3u const& begin, numtk::vec3u const& end, DataType const& value) {
            if (data.empty()) data.resize(VoxelMask::kVolume);
            for (uint32_t z = begin.z(); z < end.z(); ++z)
                for (uint32_t y = begin.y(); y < end.y(); ++y)
                    for (uint32_t x = begin.x(); x < end.x(); ++x)
                    {
                        uint32_t const index =
                            x + y*VoxelMask::kSize + z*VoxelMask::kSize*VoxelMask::kSize;
                        data[index] = value;
                    }
        }

        DataType const& operator[](numtk::vec3u const& point) const {
            static DataType const fallback{};
            if (data.empty()) return fallback;
            uint32_t const index =
                point.x() + point.y()*VoxelMask::kSize + point.z()*VoxelMask::kSize*VoxelMask::kSize;
            return data[index];
        }

        VoxelMask child_mask{};
        VoxelMask data_mask{};
        std::vector<DataType> data{};
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
