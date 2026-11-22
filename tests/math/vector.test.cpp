#include "glm_oracle.h"

import std;
import aegis.math;

namespace math = aegis::math;
using math::Double2;
using math::Double3;
using math::Double4;
using math::Float2;
using math::Float3;
using math::Float4;
using math::Int2;
using math::Int3;
using math::Int4;
using math::Uint2;
using math::Uint3;
using math::Uint4;

TYPE_TO_STRING_AS("Float2", Float2);
TYPE_TO_STRING_AS("Float3", Float3);
TYPE_TO_STRING_AS("Float4", Float4);
TYPE_TO_STRING_AS("Double2", Double2);
TYPE_TO_STRING_AS("Double3", Double3);
TYPE_TO_STRING_AS("Double4", Double4);
TYPE_TO_STRING_AS("Int2", Int2);
TYPE_TO_STRING_AS("Int3", Int3);
TYPE_TO_STRING_AS("Int4", Int4);
TYPE_TO_STRING_AS("Uint2", Uint2);
TYPE_TO_STRING_AS("Uint3", Uint3);
TYPE_TO_STRING_AS("Uint4", Uint4);

#define AEGIS_ALL_VECTORS Float2, Float3, Float4, Double2, Double3, Double4, Int2, Int3, Int4, Uint2, Uint3, Uint4
#define AEGIS_SIGNED_VECTORS Float2, Float3, Float4, Double2, Double3, Double4, Int2, Int3, Int4
#define AEGIS_FLOAT_VECTORS Float2, Float3, Float4, Double2, Double3, Double4

namespace
{
/// @brief The vector whose component i is first + i * step
template<typename V>
auto sequence(typename V::ValueType first, typename V::ValueType step) -> V
{
    V v;
    for (std::size_t i = 0; i < V::componentCount; ++i)
        v[i] = static_cast<typename V::ValueType>(first + static_cast<typename V::ValueType>(i) * step);
    return v;
}

// Compile-only checks: the constraints reject these uses, so a wrong use is a constraint failure, not a bug.
template<typename V>
concept Normalizable = requires(V v) { math::normalize(v); };
template<typename A, typename B>
concept Addable = requires(A a, B b) { a + b; };
template<typename V>
concept Negatable = requires(V v) { -v; };
template<typename V>
concept Crossable = requires(V v) { math::cross(v, v); };

static_assert(!Normalizable<Int3>);
static_assert(!Normalizable<Uint2>);
static_assert(Normalizable<Float3>);
static_assert(!Addable<Float3, Int3>);
static_assert(!Addable<Float3, Float4>);
static_assert(!Addable<Float3, Double3>);
static_assert(!Negatable<Uint3>);
static_assert(Negatable<Int3>);
static_assert(!Crossable<Float2>);
static_assert(std::is_same_v<decltype(Float3{} * 2.0), Float3>, "a scalar converts to the vector's type, never the reverse");
static_assert(!std::is_convertible_v<Int3, Float3>, "different scalar types convert only explicitly");
static_assert(std::is_constructible_v<Float3, Int3>);
static_assert(!std::is_convertible_v<float, Float3>, "the broadcast constructor is explicit");
static_assert(!std::is_convertible_v<Float4, Float3>, "dropping a component is explicit");
static_assert(!math::VectorType<float>);
static_assert(math::VectorType<Uint4>);
static_assert(math::FloatVector<Double2>);
static_assert(!math::FloatVector<Int2>);

// The types are usable in constant expressions.
static_assert(Float3{ 1.0f, 2.0f, 3.0f } + Float3{ 1.0f } == Float3{ 2.0f, 3.0f, 4.0f });
static_assert(math::dot(Int3{ 1, 2, 3 }, Int3{ 4, 5, 6 }) == 32);
}

TEST_SUITE("math::Vector")
{
    TEST_CASE_TEMPLATE("default constructs to zero and broadcasts one scalar", V, AEGIS_ALL_VECTORS)
    {
        using T = typename V::ValueType;
        const V zero;
        const V broadcast{ T{ 3 } };
        for (std::size_t i = 0; i < V::componentCount; ++i)
        {
            CHECK(zero[i] == T{ 0 });
            CHECK(broadcast[i] == T{ 3 });
        }
    }

    TEST_CASE_TEMPLATE("indexes the named components in order", V, AEGIS_ALL_VECTORS)
    {
        V v = sequence<V>(1, 1);
        CHECK(v[0] == v.x);
        CHECK(v[1] == v.y);
        v[1] = 9;
        CHECK(v.y == 9);
        if constexpr (V::componentCount >= 3)
            CHECK(v[2] == v.z);
        if constexpr (V::componentCount == 4)
            CHECK(v[3] == v.w);
    }

    TEST_CASE_TEMPLATE("adds and subtracts component-wise", V, AEGIS_ALL_VECTORS)
    {
        const V a = sequence<V>(10, 2);
        const V b = sequence<V>(1, 1);
        const V sum = a + b;
        const V difference = a - b;
        for (std::size_t i = 0; i < V::componentCount; ++i)
        {
            CHECK(sum[i] == a[i] + b[i]);
            CHECK(difference[i] == a[i] - b[i]);
        }

        V accumulated = a;
        accumulated += b;
        CHECK(accumulated == sum);
        accumulated -= b;
        CHECK(accumulated == a);
    }

    TEST_CASE_TEMPLATE("multiplies and divides component-wise and by a scalar", V, AEGIS_ALL_VECTORS)
    {
        using T = typename V::ValueType;
        const V a = sequence<V>(12, 6);
        const V b = sequence<V>(1, 1);
        const V product = a * b;
        const V quotient = a / b;
        const V scaled = a * T{ 2 };
        const V scaledLeft = T{ 2 } * a;
        const V halved = a / T{ 2 };
        for (std::size_t i = 0; i < V::componentCount; ++i)
        {
            CHECK(product[i] == a[i] * b[i]);
            CHECK(quotient[i] == a[i] / b[i]);
            CHECK(scaled[i] == a[i] * T{ 2 });
            CHECK(halved[i] == a[i] / T{ 2 });
        }
        CHECK(scaledLeft == scaled);

        V v = a;
        v *= b;
        CHECK(v == product);
        v /= b;
        CHECK(v == a);
        v *= T{ 2 };
        CHECK(v == scaled);
        v /= T{ 2 };
        CHECK(v == a);
    }

    TEST_CASE_TEMPLATE("negates signed components", V, AEGIS_SIGNED_VECTORS)
    {
        const V v = sequence<V>(-2, 3);
        CHECK(-v + v == V{});
        CHECK(-(-v) == v);
    }

    TEST_CASE_TEMPLATE("computes the dot product", V, AEGIS_ALL_VECTORS)
    {
        using T = typename V::ValueType;
        const V a = sequence<V>(1, 1);
        const V b = sequence<V>(2, 2);
        T expected{ 0 };
        for (std::size_t i = 0; i < V::componentCount; ++i)
            expected += a[i] * b[i];
        CHECK(math::dot(a, b) == expected);
    }

    TEST_CASE_TEMPLATE("applies the scalar helpers per component", V, AEGIS_ALL_VECTORS)
    {
        using T = typename V::ValueType;
        const V a = sequence<V>(1, 3);
        const V b = sequence<V>(4, 1);
        const V low{ T{ 2 } };
        const V high{ T{ 5 } };
        for (std::size_t i = 0; i < V::componentCount; ++i)
        {
            CHECK(math::min(a, b)[i] == math::min(a[i], b[i]));
            CHECK(math::max(a, b)[i] == math::max(a[i], b[i]));
            CHECK(math::clamp(a, low, high)[i] == math::clamp(a[i], T{ 2 }, T{ 5 }));
            CHECK(math::abs(a)[i] == math::abs(a[i]));
        }
        CHECK(math::clamp(a, T{ 2 }, T{ 5 }) == math::clamp(a, low, high));
    }
}

TEST_SUITE("math::Vector constructors")
{
    TEST_CASE("joins smaller vectors with scalars")
    {
        CHECK(Float3{ Float2{ 1.0f, 2.0f }, 3.0f } == Float3{ 1.0f, 2.0f, 3.0f });
        CHECK(Float4{ Float3{ 1.0f, 2.0f, 3.0f }, 4.0f } == Float4{ 1.0f, 2.0f, 3.0f, 4.0f });
        CHECK(Int4{ Int2{ 1, 2 }, 3, 4 } == Int4{ 1, 2, 3, 4 });
    }

    TEST_CASE("drops trailing components only when asked")
    {
        const Float4 v{ 1.0f, 2.0f, 3.0f, 4.0f };
        CHECK(Float3{ v } == Float3{ 1.0f, 2.0f, 3.0f });
        CHECK(Float2{ v } == Float2{ 1.0f, 2.0f });
        CHECK(Float2{ Float3{ 5.0f, 6.0f, 7.0f } } == Float2{ 5.0f, 6.0f });
        CHECK(v.xyz() == Float3{ 1.0f, 2.0f, 3.0f });
        CHECK(v.xy() == Float2{ 1.0f, 2.0f });
        CHECK(Float3{ 8.0f, 9.0f, 1.0f }.xy() == Float2{ 8.0f, 9.0f });
    }

    TEST_CASE("converts the scalar type explicitly")
    {
        CHECK(Float2{ Uint2{ 1920u, 1080u } } == Float2{ 1920.0f, 1080.0f });
        CHECK(Int3{ Float3{ 1.75f, -2.5f, 3.0f } } == Int3{ 1, -2, 3 });
        CHECK(Float3{ Double3{ 1.0e6, 2.0, -3.0 } } == Float3{ 1.0e6f, 2.0f, -3.0f });
        CHECK(Double4{ Float4{ 0.5f } } == Double4{ 0.5 });
    }
}

TEST_SUITE("math::Vector geometry")
{
    TEST_CASE_TEMPLATE("matches glm on random input", V, AEGIS_FLOAT_VECTORS)
    {
        using T = typename V::ValueType;
        const double tolerance = oracle::tolerance<T>;
        std::mt19937 rng{ 444 };
        for (int sample = 0; sample < 50; ++sample)
        {
            CAPTURE(sample);
            const V a = oracle::randomVector<V>(rng);
            const V b = oracle::randomVector<V>(rng);
            const T t = oracle::randomScalar<T>(rng) / T{ 10 };
            const auto ga = oracle::toGlmVec(a);
            const auto gb = oracle::toGlmVec(b);

            CHECK(math::dot(a, b) == oracle::near(glm::dot(ga, gb), tolerance));
            CHECK(math::length(a) == oracle::near(glm::length(ga), tolerance));
            CHECK(math::distance(a, b) == oracle::near(glm::distance(ga, gb), tolerance));
            oracle::checkVec(math::normalize(a), glm::normalize(ga), tolerance);
            oracle::checkVec(math::lerp(a, b, t), glm::mix(ga, gb, t), tolerance);
            oracle::checkVec(math::floor(a), glm::floor(ga), tolerance);
            oracle::checkVec(math::fract(a), glm::fract(ga), tolerance);
            const V divisor = math::abs(b) + V{ T{ 1 } }; // never zero
            oracle::checkVec(math::mod(a, divisor), glm::mod(ga, oracle::toGlmVec(divisor)), tolerance);
            oracle::checkVec(math::mod(a, T{ 3 }), glm::mod(ga, T{ 3 }), tolerance);
            oracle::checkVec(math::sqrt(math::abs(a)), glm::sqrt(glm::abs(ga)), tolerance);
            oracle::checkVec(math::radians(a), glm::radians(ga), tolerance);
            oracle::checkVec(math::degrees(a), glm::degrees(ga), tolerance);
            oracle::checkVec(math::saturate(a / T{ 10 }), glm::clamp(ga / T{ 10 }, T{ 0 }, T{ 1 }), tolerance);
            if constexpr (V::componentCount == 3)
                oracle::checkVec(math::cross(a, b), glm::cross(ga, gb), tolerance);
        }
    }

    TEST_CASE_TEMPLATE("normalizing the zero vector gives zero", V, AEGIS_FLOAT_VECTORS)
    {
        CHECK(math::normalize(V{}) == V{});
    }

    TEST_CASE("cross follows the right-hand rule")
    {
        CHECK(math::cross(Float3{ 1.0f, 0.0f, 0.0f }, Float3{ 0.0f, 1.0f, 0.0f }) == Float3{ 0.0f, 0.0f, 1.0f });
        CHECK(math::cross(Int3{ 0, 1, 0 }, Int3{ 1, 0, 0 }) == Int3{ 0, 0, -1 });
    }

    TEST_CASE_TEMPLATE("nearlyEqual compares every component", V, AEGIS_FLOAT_VECTORS)
    {
        using T = typename V::ValueType;
        const V a = sequence<V>(1, 1);
        V b = a;
        b[V::componentCount - 1] += math::defaultTolerance<T> / T{ 2 };
        CHECK(math::nearlyEqual(a, b));
        b[0] += T{ 1 };
        CHECK_FALSE(math::nearlyEqual(a, b));
        CHECK(math::nearlyEqual(a, b, T{ 1 }));
    }
}
