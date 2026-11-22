export module aegis.math:aliases;

import std;
import :vector;
import :matrix;
import :quaternion;

export namespace aegis::math
{
using Float2 = Vector<float, 2>;
using Float3 = Vector<float, 3>;
using Float4 = Vector<float, 4>;

using Float2x2 = Matrix<float, 2, 2>;
using Float2x3 = Matrix<float, 2, 3>;
using Float2x4 = Matrix<float, 2, 4>;
using Float3x2 = Matrix<float, 3, 2>;
using Float3x3 = Matrix<float, 3, 3>;
using Float3x4 = Matrix<float, 3, 4>;
using Float4x2 = Matrix<float, 4, 2>;
using Float4x3 = Matrix<float, 4, 3>;
using Float4x4 = Matrix<float, 4, 4>;

using Double2 = Vector<double, 2>;
using Double3 = Vector<double, 3>;
using Double4 = Vector<double, 4>;

using Double2x2 = Matrix<double, 2, 2>;
using Double2x3 = Matrix<double, 2, 3>;
using Double2x4 = Matrix<double, 2, 4>;
using Double3x2 = Matrix<double, 3, 2>;
using Double3x3 = Matrix<double, 3, 3>;
using Double3x4 = Matrix<double, 3, 4>;
using Double4x2 = Matrix<double, 4, 2>;
using Double4x3 = Matrix<double, 4, 3>;
using Double4x4 = Matrix<double, 4, 4>;

using Int2 = Vector<std::int32_t, 2>;
using Int3 = Vector<std::int32_t, 3>;
using Int4 = Vector<std::int32_t, 4>;

using Uint2 = Vector<std::uint32_t, 2>;
using Uint3 = Vector<std::uint32_t, 3>;
using Uint4 = Vector<std::uint32_t, 4>;

using Quat = Quaternion<float>;
}

namespace aegis::math
{
// Every alias has scalar layout: `count` consecutive values, no padding, no alignment beyond the scalar's, and
// copyable as bytes. This is what lets the types go to the GPU and through std::bit_cast as they are.
template<typename V>
consteval auto hasScalarLayout(std::size_t count) -> bool
{
    using T = typename V::ValueType;
    return sizeof(V) == count * sizeof(T) && alignof(V) == alignof(T) && std::is_trivially_copyable_v<V> &&
           std::is_standard_layout_v<V>;
}

static_assert(hasScalarLayout<Float2>(2));
static_assert(hasScalarLayout<Float3>(3));
static_assert(hasScalarLayout<Float4>(4));
static_assert(hasScalarLayout<Float2x2>(4));
static_assert(hasScalarLayout<Float2x3>(6));
static_assert(hasScalarLayout<Float2x4>(8));
static_assert(hasScalarLayout<Float3x2>(6));
static_assert(hasScalarLayout<Float3x3>(9));
static_assert(hasScalarLayout<Float3x4>(12));
static_assert(hasScalarLayout<Float4x2>(8));
static_assert(hasScalarLayout<Float4x3>(12));
static_assert(hasScalarLayout<Float4x4>(16));

static_assert(hasScalarLayout<Double2>(2));
static_assert(hasScalarLayout<Double3>(3));
static_assert(hasScalarLayout<Double4>(4));
static_assert(hasScalarLayout<Double2x2>(4));
static_assert(hasScalarLayout<Double2x3>(6));
static_assert(hasScalarLayout<Double2x4>(8));
static_assert(hasScalarLayout<Double3x2>(6));
static_assert(hasScalarLayout<Double3x3>(9));
static_assert(hasScalarLayout<Double3x4>(12));
static_assert(hasScalarLayout<Double4x2>(8));
static_assert(hasScalarLayout<Double4x3>(12));
static_assert(hasScalarLayout<Double4x4>(16));

static_assert(hasScalarLayout<Int2>(2));
static_assert(hasScalarLayout<Int3>(3));
static_assert(hasScalarLayout<Int4>(4));
static_assert(hasScalarLayout<Uint2>(2));
static_assert(hasScalarLayout<Uint3>(3));
static_assert(hasScalarLayout<Uint4>(4));

static_assert(hasScalarLayout<Quat>(4));
}
