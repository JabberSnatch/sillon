/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * Samuel Bourasseau wrote this file. You can do whatever you want with this
 * stuff. If we meet some day, and you think this stuff is worth it, you can
 * buy me a beer in return.
 * ----------------------------------------------------------------------------
 */

#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <immintrin.h>

namespace numtk {

template <typename scalar>
struct vec2 : public std::array<scalar, 2>
{
    scalar& x() { return this->operator[](0); }
    scalar& y() { return this->operator[](1); }

    static vec2<scalar> Constant(scalar v) { return vec2<scalar>{ v, v }; }

    scalar const& x() const { return static_cast<scalar const&>(const_cast<vec2*>(this)->x()); }
    scalar const& y() const { return static_cast<scalar const&>(const_cast<vec2*>(this)->y()); }

    vec2 operator+(vec2 const& o) const { return { x()+o.x(), y()+o.y() }; }
    vec2& operator+=(vec2 const& o) { x()+=o.x(); y()+=o.y(); return *this; }
    vec2 operator-(vec2 const& o) const { return { x()-o.x(), y()-o.y() }; }
    vec2& operator-=(vec2 const& o) { x()-=o.x(); y()-=o.y(); return *this; }
    vec2 operator*(vec2 const& o) const { return { x()*o.x(), y()*o.y() }; }
    vec2 operator*(scalar s) const { return { x()*s, y()*s }; }
    vec2 operator/(vec2 const& o) const { return { x()/o.x(), y()/o.y() }; }
    vec2 operator/(scalar s) const { return { x()/s, y()/s }; }
    vec2 operator%(vec2 const& o) const { return { x()%o.x(), y()%o.y() }; }
    vec2 operator%(scalar s) const { return { x()%s, y()%s }; }

    vec2 operator>>(vec2 const& o) const { return { x()>>o.x(), y()>>o.y() }; }
    vec2 operator>>(scalar s) const{ return { x()>>s, y()>>s }; }
    vec2 operator&(vec2 const& o) const { return { x()&o.x(), y()&o.y() }; }
    vec2 operator&(scalar s) const { return { x()&s, y()&s }; }

    scalar dot(vec2 const& o) const { return x()*o.x() + y()*o.y(); }
    float sqrnorm() const { return dot(*this); }
    float norm() const { return std::sqrt(sqrnorm()); }
    vec2 normalise() const { return *this / norm(); }
    vec2 abs() const { return { std::abs(x()), std::abs(y()) }; }

    template <typename dst>
    vec2<dst> cast() const { return vec2<dst>{ (dst)x(), (dst)y() }; }
};

template <typename scalar>
struct vec3
{
    scalar x, y, z;
#if 0
    union {
        struct {
            scalar x;
            scalar y;
            scalar z;
        } comp;

        std::array<scalar, 3> a;
    } data;
#endif

#if 0
    scalar& x() { return data.comp.x; }
    scalar& y() { return data.comp.y; }
    scalar& z() { return data.comp.z; }
#endif

    scalar& operator[](std::size_t index) { return ((scalar*)&x)[index]; }
    scalar const& operator[](std::size_t index) const {
        return static_cast<scalar const&>(const_cast<vec3*>(this)->operator[](index));
    }

    static vec3<scalar> Constant(scalar v) { return vec3<scalar>{ v, v, v }; }

#if 0
    scalar const& x() const { return static_cast<scalar const&>(const_cast<vec3*>(this)->x()); }
    scalar const& y() const { return static_cast<scalar const&>(const_cast<vec3*>(this)->y()); }
    scalar const& z() const { return static_cast<scalar const&>(const_cast<vec3*>(this)->z()); }
#endif

    vec3 operator+(vec3 const& o) const { return { x+o.x, y+o.y, z+o.z }; }
    vec3& operator+=(vec3 const& o) { x+=o.x; y+=o.y; z+=o.z; return *this; }
    vec3 operator-(vec3 const& o) const { return { x-o.x, y-o.y, z-o.z }; }
    vec3& operator-=(vec3 const& o) { x-=o.x; y-=o.y; z-=o.z; return *this; }
    vec3 operator*(vec3 const& o) const { return { x*o.x, y*o.y, z*o.z }; }
    vec3 operator*(scalar s) const { return { x*s, y*s, z*s }; }
    vec3 operator/(vec3 const& o) const { return { x/o.x, y/o.y, z/o.z }; }
    vec3 operator/(scalar s) const { return { x/s, y/s, z/s }; }
    vec3 operator%(vec3 const& o) const { return { x%o.x, y%o.y, z%o.z }; }
    vec3 operator%(scalar s) const { return { x%s, y%s, z%s }; }

    vec3 operator>>(vec3 const& o) const { return { x>>o.x, y>>o.y, z>>o.z }; }
    vec3 operator>>(scalar s) const{ return { x>>s, y>>s, z>>s }; }
    vec3 operator&(vec3 const& o) const { return { x&o.x, y&o.y, z&o.z }; }
    vec3 operator&(scalar s) const { return { x&s, y&s, z&s }; }

    bool operator<(vec3 const& o) {
        return x < o.x
            || (x == o.x
                && (y < o.y
                    || (y == o.y && z < o.z)));
    }

    scalar dot(vec3 const& o) const { return x*o.x + y*o.y + z*o.z; }
    float sqrnorm() const { return dot(*this); }
    float norm() const { return std::sqrt(sqrnorm()); }
    vec3 normalise() const { return *this / norm(); }
    vec3 abs() const { return { std::abs(x), std::abs(y), std::abs(z) }; }

    vec3 cross(vec3 const& o) const {
        std::array<scalar, 3> const& data = *(std::array<scalar, 3> const*)this;
        return vec3{
            data[1]*o[2] - data[2]*o[1],
            data[2]*o[0] - data[0]*o[2],
            data[0]*o[1] - data[1]*o[0],
        };
    }

    template <typename dst>
    vec3<dst> cast() const { return vec3<dst>{ (dst)x, (dst)y, (dst)z }; }
};

template <typename scalar>
struct vec4 : public std::array<scalar, 4>
{
    scalar& x() { return this->operator[](0); }
    scalar& y() { return this->operator[](1); }
    scalar& z() { return this->operator[](2); }
    scalar& w() { return this->operator[](3); }

    static vec4<scalar> Constant(scalar v) { return vec4<scalar>{ v, v, v, v }; }

    scalar const& x() const { return static_cast<scalar const&>(const_cast<vec4*>(this)->x()); }
    scalar const& y() const { return static_cast<scalar const&>(const_cast<vec4*>(this)->y()); }
    scalar const& z() const { return static_cast<scalar const&>(const_cast<vec4*>(this)->z()); }
    scalar const& w() const { return static_cast<scalar const&>(const_cast<vec4*>(this)->w()); }

    vec4 operator+(vec4 const& o) const { return { x()+o.x(), y()+o.y(), z()+o.z(), w()+o.w() }; }
    vec4 operator-(vec4 const& o) const { return { x()-o.x(), y()-o.y(), z()-o.z(), w()-o.w() }; }
    vec4 operator*(vec4 const& o) const { return { x()*o.x(), y()*o.y(), z()*o.z(), w()*o.w() }; }
    vec4 operator*(scalar s) const { return { x()*s, y()*s, z()*s, w()*s }; }
    vec4 operator/(vec4 const& o) const { return { x()/o.x(), y()/o.y(), z()/o.z(), w()/o.w() }; }
    vec4 operator/(scalar s) const { return { x()/s, y()/s, z()/s, w()/s }; }
    vec4 operator%(vec4 const& o) const { return { x()%o.x(), y()%o.y(), z()%o.z(), w()%o.w() }; }
    vec4 operator%(scalar s) const { return { x()%s, y()%s, z()%s, w()%s }; }

    vec4 operator>>(vec4 const& o) const { return { x()>>o.x(), y()>>o.y(), z()>>o.z(), w()>>o.w() }; }
    vec4 operator>>(scalar s) const{ return { x()>>s, y()>>s, z()>>s, w()>>s }; }
    vec4 operator&(vec4 const& o) const { return { x()&o.x(), y()&o.y(), z()&o.z(), w()&o.w() }; }
    vec4 operator&(scalar s) const { return { x()&s, y()&s, z()&s, w()&s }; }

    scalar dot(vec4 const& o) const { return x()*o.x() + y()*o.y() + z()*o.z() + w()*o.w(); }
    float sqrnorm() const { return dot(*this); }
    float norm() const { return std::sqrt(sqrnorm()); }
    vec4 normalise() const { return *this * norm(); }
    vec4 abs() const { return { std::abs(x()), std::abs(y()), std::abs(z()), std::abs(w()) }; }

    template <typename dst>
    vec4<dst> cast() const { return vec4<dst>{ (dst)x(), (dst)y(), (dst)z(), (dst)w() }; }
};

template <typename s>
vec2<s> min(vec2<s> const& lhs, vec2<s> const& rhs) {
    return {
        std::min(lhs.x(), rhs.x()), std::min(lhs.y(), rhs.y())
    };
}

template <typename s>
vec2<s> max(vec2<s> const& lhs, vec2<s> const& rhs) {
    return {
        std::max(lhs.x(), rhs.x()), std::max(lhs.y(), rhs.y())
    };
}

template <typename s>
vec3<s> min(vec3<s> const& lhs, vec3<s> const& rhs) {
    return {
        std::min(lhs.x, rhs.x), std::min(lhs.y, rhs.y),
        std::min(lhs.z, rhs.z)
    };
}

template <typename s>
vec3<s> max(vec3<s> const& lhs, vec3<s> const& rhs) {
    return {
        std::max(lhs.x, rhs.x), std::max(lhs.y, rhs.y),
        std::max(lhs.z, rhs.z)
    };
}

template <typename s>
vec4<s> min(vec4<s> const& lhs, vec4<s> const& rhs) {
    return {
        std::min(lhs.x(), rhs.x()), std::min(lhs.y(), rhs.y()),
        std::min(lhs.z(), rhs.z()), std::min(lhs.w(), rhs.w())
    };
}

template <typename s>
vec4<s> max(vec4<s> const& lhs, vec4<s> const& rhs) {
    return {
        std::max(lhs.x(), rhs.x()), std::max(lhs.y(), rhs.y()),
        std::max(lhs.z(), rhs.z()), std::max(lhs.w(), rhs.w())
    };
}


template <typename scalar>
struct mat4 : public std::array<scalar, 16>
{
    vec4<scalar>& col(uint32_t index) { return *(vec4<scalar>*)&(this->operator[](index * 4)); }

    vec4<scalar> const& col(uint32_t index) const {
        return static_cast<vec4<scalar> const&>(const_cast<mat4*>(this)->col(index));
    }

    mat4 transpose() const {
        std::array<scalar, 16> const& data = *(std::array<scalar, 16> const*)this;
        return {
            data[0], data[4], data[8], data[12],
            data[1], data[5], data[9], data[13],
            data[2], data[6], data[10], data[14],
            data[3], data[7], data[11], data[15],
        };
    }

    mat4 operator*(mat4 const& o) const {
        mat4 const lt = transpose();
        #define ColumnDot(l, r) lt.col(l).dot(o.col(r))
        return {
            ColumnDot(0, 0), ColumnDot(1, 0), ColumnDot(2, 0), ColumnDot(3, 0),
            ColumnDot(0, 1), ColumnDot(1, 1), ColumnDot(2, 1), ColumnDot(3, 1),
            ColumnDot(0, 2), ColumnDot(1, 2), ColumnDot(2, 2), ColumnDot(3, 2),
            ColumnDot(0, 3), ColumnDot(1, 3), ColumnDot(2, 3), ColumnDot(3, 3),
        };
        #undef ColumnDot
    }

    static mat4 Constant(scalar c) {
        return {
            c, c, c, c,
            c, c, c, c,
            c, c, c, c,
            c, c, c, c,
        };
    }

    // Rotate everything so Y is down, and X is right.
    // Maps Z to [0, 1]
    static mat4 Perspective_VulkanNDC(scalar n, scalar f, scalar alpha, scalar how) {
        scalar const tan_half_alpha = std::tan(alpha / scalar(2));
        return {
            -scalar(1)/tan_half_alpha, 0, 0, 0,
            0, -scalar(1)/(how*tan_half_alpha), 0, 0,
            0, 0, f/(f-n), 1,
            0, 0, -(f*n)/(f-n), 0
        };
    }

    static mat4 Orthographic(scalar w, scalar h, scalar f, scalar n) {
        return {
            2/w, 0, 0, 0,
            0, 2/h, 0, 0,
            0, 0, -2/(f-n), 0,
            0, 0, -(f+n)/(f-n), 1
        };
    }

    // Forward is in the direction the camera is looking down
    static mat4 View(vec3<scalar> const& _f, vec3<scalar> const& _u, vec3<scalar> const& _p) {
        using vec3s = vec3<scalar>;
        vec3s const l = _u.cross(_f).normalise();
        vec3s const t{ _p.dot(l), _p.dot(_u), _p.dot(_f) };
        return {
            l[0], _u[0], _f[0], 0,
            l[1], _u[1], _f[1], 0,
            l[2], _u[2], _f[2], 0,
            -t[0], -t[1], -t[2], 1
        };
    }

    static mat4 InvView(vec3<scalar> const& _f, vec3<scalar> const& _u, vec3<scalar> const& _p) {
        using vec3s = vec3<scalar>;
        vec3s const l = _u.cross(_f).normalise();
        return {
            l[0], l[1], l[2], 0,
            _u[0], _u[1], _u[2], 0,
            _f[0], _f[1], _f[2], 0,
            _p[0], _p[1], _p[2], 1
        };
    }
};

template <typename scalar>
struct quat : public std::array<scalar, 4>
{
    scalar& real() { return this->operator[](0); }
    scalar& i() { return this->operator[](1); }
    scalar& j() { return this->operator[](2); }
    scalar& k() { return this->operator[](3); }

    scalar const& real() const { return static_cast<scalar const&>(const_cast<quat*>(this)->real()); }
    scalar const& i() const { return static_cast<scalar const&>(const_cast<quat*>(this)->i()); }
    scalar const& j() const { return static_cast<scalar const&>(const_cast<quat*>(this)->j()); }
    scalar const& k() const { return static_cast<scalar const&>(const_cast<quat*>(this)->k()); }

    static quat Identity() { return { 1.f, 0.f, 0.f, 0.f }; }

    static quat AngleAxis(scalar _angle, numtk::vec3<scalar> const& _axis)
    {
        scalar const half_angle = _angle / 2;
        scalar const sin_ha = std::sin(half_angle);
        return quat{
            std::cos(half_angle),
            _axis[0] * sin_ha,
            _axis[1] * sin_ha,
            _axis[2] * sin_ha
        };
    }

    quat operator+(quat const& o) {
        return (vec4<scalar> const&)o + *(vec4<scalar> const*)this;
    }

    quat operator*(quat const& o) {
        std::array<scalar, 4> const& data = *(std::array<scalar, 4> const*)this;
        return {
            data[0]*o[0] - data[1]*o[1] - data[2]*o[2] - data[3]*o[3],
            data[0]*o[1] + data[1]*o[0] + data[2]*o[3] - data[3]*o[2],
            data[0]*o[2] - data[1]*o[3] + data[2]*o[0] + data[3]*o[1],
            data[0]*o[3] + data[1]*o[2] - data[2]*o[1] + data[3]*o[0]
        };
    }

    quat& operator*=(quat const& o) {
        *this = *this * o;
        return *this;
    }

    template <typename vscalar>
    vec3<vscalar> operator*(vec3<vscalar> const& v) const {
        // https://blog.molecular-matters.com/2013/05/24/a-faster-quaternion-vector-multiplication/
        vec3<vscalar> t = 2.f * ((vec3<scalar> const*)&(this->i()))->cross(v);
        vec3<vscalar> r = v + real() * t + ((vec3<scalar> const*)&(this->i()))->cross(t);
        return r;
    }
};

template <typename scalar>
struct bounds2
{
    using PointType = vec2<scalar>;
    PointType min;
    PointType extent;
    bool Contains(PointType const& point) {
        PointType max = min+extent;
        return point.x() >= min.x() && point.x() < max.x()
            && point.y() >= min.y() && point.y() < max.y();
    }
};

template <typename scalar>
struct bounds3
{
    using PointType = vec3<scalar>;
    PointType min;
    PointType extent;
    bool Contains(PointType const& point) {
        PointType max = min+extent;
        return point.x >= min.x && point.x < max.x
            && point.y >= min.y && point.y < max.y
            && point.z >= min.z && point.z < max.z;
    }
};

using vec2f = vec2<float>;
using vec3f = vec3<float>;
using vec4f = vec4<float>;
using vec2u = vec2<uint32_t>;
using vec3u = vec3<uint32_t>;
using vec2i = vec2<int32_t>;
using vec3i = vec3<int32_t>;
using mat4f = mat4<float>;
using quatf = quat<float>;

using bounds2f = bounds2<float>;
using bounds2u = bounds2<uint32_t>;
using bounds2i = bounds2<int32_t>;
using bounds3i = bounds3<int32_t>;

template <uint32_t B>
inline uint32_t
ilogN(uint32_t _v)
{
    if (_v == 0) return ~0u;
    uint32_t res = 0u;
    while (_v /= B) { ++res; }
    return res;
}

inline uint64_t
BitReverse(uint64_t _v)
{
    _v = (((_v & 0xAAAAAAAAAAAAAAAAull) >> 1) | ((_v & 0x5555555555555555ull) << 1));
    _v = (((_v & 0xCCCCCCCCCCCCCCCCull) >> 2) | ((_v & 0x3333333333333333ull) << 2));
    _v = (((_v & 0xF0F0F0F0F0F0F0F0ull) >> 4) | ((_v & 0x0F0F0F0F0F0F0F0Full) << 4));
    _v = (((_v & 0xFF00FF00FF00FF00ull) >> 8) | ((_v & 0x00FF00FF00FF00FFull) << 8));
    _v = (((_v & 0xFFFF0000FFFF0000ull) >> 16) | ((_v & 0x0000FFFF0000FFFFull) << 16));
    return (_v >> 32) | (_v << 32);
}

inline uint32_t
BitExtract(uint32_t _v, uint32_t _m)
{
    return (uint32_t)_pext_u32(_v, _m);
}

inline uint64_t
BitExtract(uint64_t _v, uint64_t _m)
{
    return (uint64_t)_pext_u64(_v, _m);
}

inline uint64_t
BitDeposit(uint64_t _v, uint64_t _m)
{
    return (uint64_t)_pdep_u64(_v, _m);
}

} // namespace numtk

template <typename scalar>
inline numtk::vec2<scalar> operator*(scalar s, numtk::vec2<scalar> const& v) { return v*s; }
template <typename scalar>
inline numtk::vec3<scalar> operator*(scalar s, numtk::vec3<scalar> const& v) { return v*s; }
template <typename scalar>
inline numtk::vec4<scalar> operator*(scalar s, numtk::vec4<scalar> const& v) { return v*s; }

namespace numtk {

using Vec2i_t = std::array<int, 2>;
using Vec3i64_t = std::array<std::int64_t, 3>;
using Vec2_t = std::array<float, 2>;
using Vec3_t = std::array<float, 3>;
using Vec4_t = std::array<float, 4>;
using Mat3_t = std::array<float, 9>;
using Mat4_t = std::array<float, 16>;
using Quat_t = std::array<float, 4>;
using dQuat_t = std::array<float, 8>;
using SH2nd_t = std::array<float, 9>;

constexpr float kPi = 3.1415926535f;
constexpr float kInvPi = 1.f / kPi;

constexpr float kDegrees = kPi / 180.f;

inline uint32_t
log2(uint32_t _op)
{
    if (_op == 0) return ~0u;
    uint32_t res = 0u;
    while (_op >>= 1) { ++res; }
    return res;
}

inline Vec2i_t
vec2i_sub(Vec2i_t const& _lhs, Vec2i_t const&_rhs)
{
    return Vec2i_t{ _lhs[0] - _rhs[0], _lhs[1] - _rhs[1] };
}

inline Vec2i_t
vec2i_int_div(Vec2i_t const& _lhs, int _rhs)
{
    return Vec2i_t{ _lhs[0] / _rhs, _lhs[1] / _rhs };
}

inline bool
vec2i_equal(Vec2i_t const& _lhs, Vec2i_t const& _rhs)
{
    return (_lhs[0] == _rhs[0]) && (_lhs[1] == _rhs[1]);
}

inline Vec2_t
vec2_itof(Vec2i_t const& _in)
{
    return Vec2_t{ (float)_in[0], (float)_in[1] };
}

inline Vec3i64_t
vec3i64_add(Vec3i64_t const& _lhs, Vec3i64_t const& _rhs)
{
    return Vec3i64_t{ _lhs[0]+_rhs[0], _lhs[1]+_rhs[1], _lhs[2]+_rhs[2] };
}

inline Vec3i64_t
vec3i64_sub(Vec3i64_t const& _lhs, Vec3i64_t const& _rhs)
{
    return Vec3i64_t{ _lhs[0]-_rhs[0], _lhs[1]-_rhs[1], _lhs[2]-_rhs[2] };
}

inline Vec3i64_t
vec3i64_int_mul(Vec3i64_t const& _lhs, std::int64_t _rhs)
{
    return Vec3i64_t{ _lhs[0]*_rhs, _lhs[1]*_rhs, _lhs[2]*_rhs };
}

inline Vec3i64_t
vec3i64_int_div(Vec3i64_t const& _lhs, std::int64_t _rhs)
{
    return Vec3i64_t{ _lhs[0]/_rhs, _lhs[1]/_rhs, _lhs[2]/_rhs };
}

inline Vec3_t
vec3_from_vec4(Vec4_t const& _o)
{
    return Vec3_t{ _o[0], _o[1], _o[2] };
}

inline Vec3_t
vec3_constant(float _op)
{
    return Vec3_t{ _op, _op, _op };
}

inline Vec3_t
vec3_sub(Vec3_t const& _lhs, Vec3_t const& _rhs)
{
    return Vec3_t{ _lhs[0]-_rhs[0], _lhs[1]-_rhs[1], _lhs[2]-_rhs[2] };
}

inline Vec3_t
vec3_add(Vec3_t const& _lhs, Vec3_t const& _rhs)
{
    return Vec3_t{ _lhs[0]+_rhs[0], _lhs[1]+_rhs[1], _lhs[2]+_rhs[2] };
}

inline Vec3_t
vec3_float_mul(Vec3_t const& _lhs, float _rhs)
{
    return Vec3_t{ _lhs[0]*_rhs, _lhs[1]*_rhs, _lhs[2]*_rhs };
}

inline Vec3_t
vec3_cwise_mul(Vec3_t const& _lhs, Vec3_t const& _rhs)
{
    return Vec3_t{ _lhs[0]*_rhs[0], _lhs[1]*_rhs[1], _lhs[2]*_rhs[2] };
}

template <typename FT>
inline Vec3_t
vec3_unary_op(Vec3_t const& _lhs, FT _op)
{
    return Vec3_t{ _op(_lhs[0]), _op(_lhs[1]), _op(_lhs[2]) };
}

inline float
vec3_dot(Vec3_t const& _lhs, Vec3_t const& _rhs)
{
    return _lhs[0]*_rhs[0] + _lhs[1]*_rhs[1] + _lhs[2]*_rhs[2];
}

inline Vec3_t
vec3_cross(Vec3_t const& _lhs, Vec3_t const& _rhs)
{
    return Vec3_t{
        _lhs[1]*_rhs[2] - _lhs[2]*_rhs[1],
        _lhs[2]*_rhs[0] - _lhs[0]*_rhs[2],
        _lhs[0]*_rhs[1] - _lhs[1]*_rhs[0]
    };
}

inline float
vec3_norm(Vec3_t const& _op)
{
    return std::sqrt(vec3_dot(_op, _op));
}

inline float
vec3_sqrnorm(Vec3_t const& _op)
{
    return vec3_dot(_op, _op);
}

inline Vec3_t
vec3_normalise(Vec3_t const& _op)
{
    return vec3_float_mul(_op, 1.f/std::sqrt(vec3_dot(_op, _op)));
}

inline Vec4_t
vec3_float_concat(Vec3_t const& _lhs, float const& _rhs)
{
    return Vec4_t{ _lhs[0], _lhs[1], _lhs[2], _rhs };
}


inline Vec4_t
vec4_add(Vec4_t const& _lhs, Vec4_t const& _rhs)
{
    return Vec4_t{ _lhs[0] + _rhs[0], _lhs[1] + _rhs[1], _lhs[2] + _rhs[2], _lhs[3] + _rhs[3] };
}

inline Vec4_t
vec4_sub(Vec4_t const& _lhs, Vec4_t const& _rhs)
{
    return Vec4_t{ _lhs[0] - _rhs[0], _lhs[1] - _rhs[1], _lhs[2] - _rhs[2], _lhs[3] - _rhs[3] };
}

template <typename FT>
inline Vec4_t
vec4_unary_op(Vec4_t const& _lhs, FT _op)
{
    return Vec4_t{ _op(_lhs[0]), _op(_lhs[1]), _op(_lhs[2]), _op(_lhs[3]) };
}

inline Vec4_t
vec4_float_mul(Vec4_t const& _lhs, float _rhs)
{
    return Vec4_t{ _lhs[0] * _rhs, _lhs[1] * _rhs, _lhs[2] * _rhs, _lhs[3] * _rhs};
}

inline float
vec4_dot(Vec4_t const& _lhs, Vec4_t const& _rhs)
{
    return _lhs[0]*_rhs[0] + _lhs[1]*_rhs[1] + _lhs[2]*_rhs[2] + _lhs[3]*_rhs[3];
}

inline float
vec4_sqrnorm(Vec4_t const& _op)
{
    return vec4_dot(_op, _op);
}

inline Vec3_t
mat3_vec3_mul(Mat3_t const& _lhs, Vec3_t const& _rhs)
{
    return Vec3_t{
        _lhs[0]*_rhs[0] + _lhs[3]*_rhs[1] + _lhs[6]*_rhs[2],
        _lhs[1]*_rhs[0] + _lhs[4]*_rhs[1] + _lhs[7]*_rhs[2],
        _lhs[2]*_rhs[0] + _lhs[5]*_rhs[1] + _lhs[8]*_rhs[2]
    };
}

inline Mat4_t
mat4_id()
{
    return Mat4_t{
        1.f, 0.f, 0.f, 0.f,
        0.f, 1.f, 0.f, 0.f,
        0.f, 0.f, 1.f, 0.f,
        0.f, 0.f, 0.f, 1.f
    };
}

inline Mat4_t
mat4_col(Vec4_t const& _c0, Vec4_t const& _c1, Vec4_t const& _c2, Vec4_t const& _c3)
{
    return Mat4_t{
        _c0[0], _c0[1], _c0[2], _c0[3],
        _c1[0], _c1[1], _c1[2], _c1[3],
        _c2[0], _c2[1], _c2[2], _c2[3],
        _c3[0], _c3[1], _c3[2], _c3[3]
        };
}

inline Mat4_t
mat4_transpose(Mat4_t const& _op)
{
    return Mat4_t{
        _op[0], _op[4], _op[8], _op[12],
        _op[1], _op[5], _op[9], _op[13],
        _op[2], _op[6], _op[10], _op[14],
        _op[3], _op[7], _op[11], _op[15]
    };
}

inline Vec4_t
mat4_vec4_mul(Mat4_t const& _lhs, Vec4_t const& _rhs)
{
    return Vec4_t{
        _lhs[0]*_rhs[0] + _lhs[4]*_rhs[1] + _lhs[8]*_rhs[2] + _lhs[12]*_rhs[3],
        _lhs[1]*_rhs[0] + _lhs[5]*_rhs[1] + _lhs[9]*_rhs[2] + _lhs[13]*_rhs[3],
        _lhs[2]*_rhs[0] + _lhs[6]*_rhs[1] + _lhs[10]*_rhs[2] + _lhs[14]*_rhs[3],
        _lhs[3]*_rhs[0] + _lhs[7]*_rhs[1] + _lhs[11]*_rhs[2] + _lhs[15]*_rhs[3]
    };
}

inline Mat4_t
mat4_mul(Mat4_t const& _lhs, Mat4_t const& _rhs)
{
    Mat4_t const lt = mat4_transpose(_lhs);
    Vec4_t const& _lc0 = *(Vec4_t*)&lt;
    Vec4_t const& _lc1 = *(((Vec4_t*)&lt) + 1);
    Vec4_t const& _lc2 = *(((Vec4_t*)&lt) + 2);
    Vec4_t const& _lc3 = *(((Vec4_t*)&lt) + 3);

    Vec4_t const& _rc0 = *(Vec4_t*)&_rhs;
    Vec4_t const& _rc1 = *(((Vec4_t*)&_rhs) + 1);
    Vec4_t const& _rc2 = *(((Vec4_t*)&_rhs) + 2);
    Vec4_t const& _rc3 = *(((Vec4_t*)&_rhs) + 3);

    return mat4_col(
        Vec4_t{vec4_dot(_lc0, _rc0), vec4_dot(_lc1, _rc0), vec4_dot(_lc2, _rc0), vec4_dot(_lc3, _rc0)},
        Vec4_t{vec4_dot(_lc0, _rc1), vec4_dot(_lc1, _rc1), vec4_dot(_lc2, _rc1), vec4_dot(_lc3, _rc1)},
        Vec4_t{vec4_dot(_lc0, _rc2), vec4_dot(_lc1, _rc2), vec4_dot(_lc2, _rc2), vec4_dot(_lc3, _rc2)},
        Vec4_t{vec4_dot(_lc0, _rc3), vec4_dot(_lc1, _rc3), vec4_dot(_lc2, _rc3), vec4_dot(_lc3, _rc3)}
    );
}

inline Mat4_t
mat4_rot(Vec3_t const& _eulerDeg)
{
    Vec3_t er = vec3_float_mul(_eulerDeg, 3.1415926536f/180.f);

    Mat4_t x{
        1.f, 0.f, 0.f, 0.f,
        0.f, std::cos(er[0]), std::sin(er[0]), 0.f,
        0.f, -std::sin(er[0]), std::cos(er[0]), 0.f,
        0.f, 0.f, 0.f, 1.f
    };
    Mat4_t y{
        std::cos(er[1]), 0.f, -std::sin(er[1]), 0.f,
        0.f, 1.f, 0.f, 0.f,
        std::sin(er[1]), 0.f, std::cos(er[1]), 0.f,
        0.f, 0.f, 0.f, 1.f
    };
    Mat4_t z{
        std::cos(er[2]), std::sin(er[2]), 0.f, 0.f,
        -std::sin(er[2]), std::cos(er[2]), 0.f, 0.f,
        0.f, 0.f, 1.f, 0.f,
        0.f, 0.f, 0.f, 1.f
    };

    return mat4_mul(y, mat4_mul(x, z));
}

inline Mat4_t
mat4_from_quat(Quat_t const& _op)
{
     return Mat4_t{
        1.f - 2.f*(_op[2]*_op[2] + _op[3]*_op[3]),
        2.f*(_op[1]*_op[2] + _op[3]*_op[0]),
        2.f*(_op[1]*_op[3] - _op[2]*_op[0]),
        0.f,

        2.f*(_op[1]*_op[2] - _op[3]*_op[0]),
        1.f - 2.f*(_op[1]*_op[1] + _op[3]*_op[3]),
        2.f*(_op[2]*_op[3] + _op[1]*_op[0]),
        0.f,

        2.f*(_op[1]*_op[3] + _op[2]*_op[0]),
        2.f*(_op[2]*_op[3] - _op[1]*_op[0]),
        1.f - 2.f*(_op[1]*_op[1] + _op[2]*_op[2]),
        0.f,

        0.f, 0.f, 0.f, 1.f
    };
}

/*

mat4 perspective_hfov(float n, float f, float alpha, float aspect)
{
	float inverse_tan_half_alpha = 1.0 / tan(alpha * 0.5);
	return mat4(
		inverse_tan_half_alpha, 0.0, 0.0, 0.0,
		0.0, (1.0/aspect)*inverse_tan_half_alpha, 0.0, 0.0,
		0.0, 0.0, (-(f+n))/(f-n), -1.0,
		0.0, 0.0, (-2.0*f*n)/(f-n), 0.0
	);
}

*/

inline Mat4_t
mat4_perspective(float n, float f, float alpha, float how)
{
    const float inv_tan_half_alpha = 1.f / std::tan(alpha * .5f);
    const float inv_fmn = 1.f / (f-n);
    return Mat4_t{
        inv_tan_half_alpha, 0.f, 0.f, 0.f,
        0.f, (1.f/how)*inv_tan_half_alpha, 0.f, 0.f,
        0.f, 0.f, (-(f+n))*inv_fmn, -1.f,
        0.f, 0.f, -2.f*f*n*inv_fmn, 0.f
    };
}

/*

mat4 perspective_inverse_hfov(float n, float f, float alpha, float aspect)
{
	float tan_half_alpha = tan(alpha * 0.5) / 1.0;
	return mat4(
		tan_half_alpha, 0.0, 0.0, 0.0,
		0.0, aspect * tan_half_alpha, 0.0, 0.0,
		0.0, 0.0, 0.0, (f-n)/(-2.0*f*n),
		0.0, 0.0, -1.0, (f+n)/(2.0*f*n)
	);
}

 */

inline Mat4_t
mat4_perspectiveinv(float n, float f, float alpha, float how)
{
	float tan_half_alpha = tan(alpha * 0.5f);
	return Mat4_t{
		tan_half_alpha, 0.0f, 0.0f, 0.0f,
		0.0f, how * tan_half_alpha, 0.0f, 0.0f,
		0.0f, 0.0f, 0.0f, (f-n)/(-2.0f*f*n),
		0.0f, 0.0f, -1.0f, (f+n)/(2.0f*f*n)
	};
}

inline Mat4_t
mat4_view(Vec3_t const& _f, Vec3_t const& _u, Vec3_t const& _p)
{
    numtk::Vec3_t const l = numtk::vec3_normalise(numtk::vec3_cross(_u, _f));
    numtk::Vec3_t const p = numtk::vec3_float_mul(_p, -1.f);
    numtk::Vec3_t const t{ numtk::vec3_dot(p, l), numtk::vec3_dot(p, _u), numtk::vec3_dot(p, _f) };

    return numtk::mat4_col(numtk::Vec4_t{l[0], _u[0], _f[0], 0.f},
                           numtk::Vec4_t{l[1], _u[1], _f[1], 0.f},
                           numtk::Vec4_t{l[2], _u[2], _f[2], 0.f},
                           numtk::vec3_float_concat(t, 1.f));
}

inline Mat4_t
mat4_viewinv(Vec3_t const& _f, Vec3_t const& _u, Vec3_t const& _p)
{
    numtk::Vec3_t const u = numtk::vec3_float_mul(_u, 1.f);
    numtk::Vec3_t const f = numtk::vec3_float_mul(_f, 1.f);
    numtk::Vec3_t const l = numtk::vec3_normalise(numtk::vec3_cross(u, f));

    return numtk::mat4_col(numtk::vec3_float_concat(l, 0.f),
                           numtk::vec3_float_concat(u, 0.f),
                           numtk::vec3_float_concat(f, 0.f),
                           numtk::vec3_float_concat(_p, 1.f));
}

inline Quat_t
quat_angle_axis(float _angle, numtk::Vec3_t const &_axis)
{
    float const half_angle = _angle / 2.f;
    float const sin_ha = std::sin(half_angle);
    return Quat_t{ std::cos(half_angle), _axis[0] * sin_ha, _axis[1] * sin_ha, _axis[2] * sin_ha };
}

inline Quat_t
quat_add(Quat_t const& _lhs, Quat_t const& _rhs)
{
    return (Quat_t)vec4_add((Vec4_t)_lhs, (Vec4_t)_rhs);
}

inline Quat_t
quat_mul(Quat_t const& _lhs, Quat_t const& _rhs)
{
    // 1 i j k
    // i −1 k −j
    // j −k −1 i
    // k j −i −1

#if 0
    Vec3_t const& v0 = *(Vec3_t const*)&(_lhs[1]);
    Vec3_t const& v1 = *(Vec3_t const*)&(_rhs[1]);

    Vec3_t v = vec3_add(vec3_float_mul(v1, _lhs[0]),
                        vec3_add(vec3_float_mul(v0, _rhs[0]),
                                 vec3_cross(v0, v1)));
    return Quat_t{
        _lhs[0]*_rhs[0] - vec3_dot(*(Vec3_t const*)&(_lhs[1]), *(Vec3_t const*)&(_rhs[1])),
        v[0], v[1], v[2]
    };
#else
    return Quat_t{
        _lhs[0]*_rhs[0] - _lhs[1]*_rhs[1] - _lhs[2]*_rhs[2] - _lhs[3]*_rhs[3],
        _lhs[0]*_rhs[1] + _lhs[1]*_rhs[0] + _lhs[2]*_rhs[3] - _lhs[3]*_rhs[2],
        _lhs[0]*_rhs[2] - _lhs[1]*_rhs[3] + _lhs[2]*_rhs[0] + _lhs[3]*_rhs[1],
        _lhs[0]*_rhs[3] + _lhs[1]*_rhs[2] - _lhs[2]*_rhs[1] + _lhs[3]*_rhs[0]
    };
#endif
}

inline Quat_t
quat_float_mul(Quat_t const& _lhs, float _rhs)
{
    return (Quat_t)vec4_float_mul((Vec4_t)_lhs, _rhs);
}

inline float
quat_norm(Quat_t const& _op)
{
    return std::sqrt(vec4_dot((Vec4_t)_op, (Vec4_t)_op));
}

inline Quat_t
quat_normalise(Quat_t const& _op)
{
    return quat_float_mul(_op, 1.f / quat_norm(_op));
}

inline Quat_t
quat_conjugate(Quat_t const& _op)
{
    return Quat_t{ _op[0], -_op[1], -_op[2], -_op[3] };
}

inline Quat_t
quat_from_euler(numtk::Vec3_t const& _e)
{
#if 1
    Quat_t x = quat_angle_axis(_e[0], {1.f, 0.f, 0.f});
    Quat_t y = quat_angle_axis(_e[1], {0.f, 1.f, 0.f});
    Quat_t z = quat_angle_axis(_e[2], {0.f, 0.f, 1.f});
    return quat_mul(y, quat_mul(x, z));

#else
    numtk::Vec3_t h = numtk::vec3_float_mul(_e, .5f);
    numtk::Vec3_t s{ std::sin(h[0]), std::sin(h[1]), std::sin(h[2]) };
    numtk::Vec3_t c{ std::cos(h[0]), std::cos(h[1]), std::cos(h[2]) };

    return Quat_t{
        c[2]*c[0]*c[1] + s[2]*s[0]*s[1],
        s[2]*c[0]*c[1] - c[2]*s[0]*s[1],
        c[2]*s[0]*c[1] + s[2]*c[0]*s[1],
        c[2]*c[0]*s[1] - s[2]*s[0]*c[1]
    };
#endif
}

inline Quat_t
quat_slerp(Quat_t const& _lhs, Quat_t const& _rhs, float _t)
{
#if 0
    float costh = vec3_dot(*(Vec3_t*)&_lhs[1], *(Vec3_t*)&_rhs[1]);
    float sign = (costh < 0.f) ? -1.f : 1.f;
    float theta = std::acos(costh);
    if (std::abs(theta) < 0.0001f)
        return _rhs;

    float t_theta = _t * theta;
    float sinth = std::sin(theta);
    float f0 = std::sin(theta - t_theta) / sinth;
    float f1 = sign * (std::sin(t_theta) / sinth);
    Quat_t result = quat_add(quat_float_mul(_lhs, f0), quat_float_mul(_rhs, f1));
    return result;
#else

    // A Fast and Accurate Algorithm for Computing SLERP, Eberly 2011
    // Port of https://github.com/CesiumCG/cesium implementation

    static constexpr float kopmu = 1.90110745351730037f;

    /*
    for (var i = 0; i < 7; ++i) {
        var s = i + 1.0;
        var t = 2.0 * s + 1.0;
        u[i] = 1.0 / (s * t);
        v[i] = s / t;
    }

    u[7] = opmu / (8.0 * 17.0);
    v[7] = opmu * 8.0 / 17.0;
    */

    static const float u[8] = {
        1.f / 3.f,
        1.f / 10.f,
        1.f / 21.f,
        1.f / 36.f,
        1.f / 55.f,
        1.f / 78.f,
        1.f / 105.f,
        kopmu / 136.f
    };

    static const float v[8] = {
        1.f / 3.f,
        2.f / 5.f,
        3.f / 7.f,
        4.f / 9.f,
        5.f / 11.f,
        6.f / 13.f,
        7.f / 15.f,
        kopmu * 8.f / 17.f
    };

    float dot = vec4_dot((Vec4_t const&)_lhs, (Vec4_t const&)_rhs);
    float const sign = (dot < 0.f) ? -1.f : 1.f;
    dot = sign * dot;

    float const dotminus1 = dot - 1.f;
    float const d = 1.f - _t;
    float const sqrT = _t * _t;
    float const sqrD = d * d;

    float bT[8];
    float bD[8];
    for (int i = 0; i < 8; ++i)
    {
        bT[i] = (u[i] * sqrT - v[i]) * dotminus1;
        bD[i] = (u[i] * sqrD - v[i]) * dotminus1;
    }

    float const cT = sign * _t * (
        1.f + bT[0] * (
            1.f + bT[1] * (
                1.f + bT[2] * (
                    1.f + bT[3] * (
                        1.f + bT[4] * (
                            1.f + bT[5] * (
                                1.f + bT[6] * (
                                    1.f + bT[7]))))))));

    float const cD = d *(
        1.f + bD[0] * (
            1.f + bD[1] * (
                1.f + bD[2] * (
                    1.f + bD[3] * (
                        1.f + bD[4] * (
                            1.f + bD[5] * (
                                1.f + bD[6] * (
                                    1.f + bD[7]))))))));

    Quat_t result = quat_add(quat_float_mul(_lhs, cD), quat_float_mul(_rhs, cT));
    return result;
#endif
}

inline dQuat_t
dquat(float _angle, Vec3_t _axis, Vec3_t _t)
{
    Quat_t const r = quat_angle_axis(_angle, _axis);
    Quat_t const t{ 0, _t[0], _t[1], _t[2] };
    Quat_t const dual = quat_float_mul(quat_mul(t, r), .5f);
    return dQuat_t{ r[0], r[1], r[2], r[3], dual[0], dual[1], dual[2], dual[3] };
}

inline Quat_t const&
dquat_rotation(dQuat_t const& _op)
{
    return *(Quat_t const*)&_op;
}

inline SH2nd_t
sh_second_order(Vec3_t _w)
{
    return SH2nd_t{
        .5f * std::sqrt(kInvPi),
        _w[1] * std::sqrt(.75f * kInvPi),
        _w[2] * std::sqrt(.75f * kInvPi),
        _w[0] * std::sqrt(.75f * kInvPi),
        .5f * _w[0]*_w[2] * std::sqrt(15.f * kInvPi),
        .5f * _w[2]*_w[1] * std::sqrt(15.f * kInvPi),
        .25f * (-_w[0]*_w[0] - _w[2]*_w[2] + 2.f*_w[1]*_w[1]) * std::sqrt(5.f * kInvPi),
        .5f * _w[0]*_w[1] * std::sqrt(15.f * kInvPi),
        .25f * (_w[0]*_w[0] - _w[2]*_w[2]) * std::sqrt(15.f * kInvPi)
    };
}

inline SH2nd_t
sh2nd_float_mul(SH2nd_t const& _lhs, float _rhs)
{
    return SH2nd_t{
        _lhs[0] * _rhs,
        _lhs[1] * _rhs,
        _lhs[2] * _rhs,
        _lhs[3] * _rhs,
        _lhs[4] * _rhs,
        _lhs[5] * _rhs,
        _lhs[6] * _rhs,
        _lhs[7] * _rhs,
        _lhs[8] * _rhs
    };
}

inline SH2nd_t
sh2nd_add(SH2nd_t const& _lhs, SH2nd_t const& _rhs)
{
    return SH2nd_t{
        _lhs[0] + _rhs[0],
        _lhs[1] + _rhs[1],
        _lhs[2] + _rhs[2],
        _lhs[3] + _rhs[3],
        _lhs[4] + _rhs[4],
        _lhs[5] + _rhs[5],
        _lhs[6] + _rhs[6],
        _lhs[7] + _rhs[7],
        _lhs[8] + _rhs[8]
    };
}

} // namespace numtk


