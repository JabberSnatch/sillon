#pragma once

#include "numtk.hh"
#include "dstk.hh"

namespace voxtk
{

struct ActivityMask
{
    static constexpr int32_t kSize = 8;

    numtk::vec3i PointModulus(numtk::vec3i const& point) const;
    numtk::vec3i PointQuotient(numtk::vec3i const& point) const;

    bool Test(numtk::vec3i const& point) const;
    void Set(numtk::vec3i const& point, bool v);
    uint64_t Kernel4(numtk::vec3i const& base) const;

    uint64_t bits[8];
};

struct Region
{
    Region(numtk::vec3i _size)
        : size{ _size }
    {}

    bool Test(numtk::vec3i const& point) const {
        if (!root) return false;

        return false;
    }

    void Set(numtk::vec3i const& point, bool v) {
    }

    struct Node {
        Node* parent;
        numtk::vec3i location;
        uint32_t depth;

        ActivityMask mask;
        dstk::OrderedVector<numtk::vec3i, Node*> children;
    };

    Node* root = nullptr;
    dstk::ObjectPool<Node> node_pool{};
    numtk::vec3i size;
};

}
