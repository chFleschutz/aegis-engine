#include "glm_oracle.h"

import std;
import aegis.math;

namespace math = aegis::math;
using math::Double2x2;
using math::Double3x3;
using math::Double3x4;
using math::Double4x4;
using math::Float2x2;
using math::Float2x3;
using math::Float3;
using math::Float3x2;
using math::Float3x3;
using math::Float3x4;
using math::Float4;
using math::Float4x3;
using math::Float4x4;

TYPE_TO_STRING_AS("Float2x2", Float2x2);
TYPE_TO_STRING_AS("Float3x3", Float3x3);
TYPE_TO_STRING_AS("Float3x4", Float3x4);
TYPE_TO_STRING_AS("Float4x4", Float4x4);
TYPE_TO_STRING_AS("Double2x2", Double2x2);
TYPE_TO_STRING_AS("Double3x3", Double3x3);
TYPE_TO_STRING_AS("Double3x4", Double3x4);
TYPE_TO_STRING_AS("Double4x4", Double4x4);

// No integer matrix aliases until a first use, but the generic core must already work for them.
using Int3x3 = math::Matrix<std::int32_t, 3, 3>;
using Uint2x4 = math::Matrix<std::uint32_t, 2, 4>;
TYPE_TO_STRING_AS("Int3x3", Int3x3);
TYPE_TO_STRING_AS("Uint2x4", Uint2x4);

#define AEGIS_MATRICES Float2x2, Float3x3, Float3x4, Float4x4, Double2x2, Double3x3, Double3x4, Double4x4
#define AEGIS_ALL_MATRICES AEGIS_MATRICES, Int3x3, Uint2x4
#define AEGIS_SQUARE_MATRICES Float2x2, Float3x3, Float4x4, Double2x2, Double3x3, Double4x4

namespace
{
template<typename M>
using RowOf = math::Vector<typename M::ValueType, M::columnCount>;
template<typename M>
using ColumnOf = math::Vector<typename M::ValueType, M::rowCount>;

/// @brief The matrix whose element [r, c] is 10 * r + c: every element distinct, and readable in a failure
template<typename M>
auto indexed() -> M
{
    auto m = M::zero();
    for (std::size_t r = 0; r < M::rowCount; ++r)
    {
        for (std::size_t c = 0; c < M::columnCount; ++c)
            m[r, c] = static_cast<typename M::ValueType>(10 * r + c);
    }
    return m;
}

/// @brief A random matrix far enough from singular that its inverse is well-conditioned
template<typename M, typename Rng>
auto randomInvertible(Rng& rng) -> M
{
    using T = typename M::ValueType;
    for (;;)
    {
        // Diagonally weighted, so most draws are well-conditioned; the determinant filters the rest.
        auto m = oracle::randomMatrix<M>(rng) + M::identity() * T{ 20 };
        if (math::abs(math::determinant(m)) > T{ 1 })
            return m;
    }
}

// Compile-only checks: every glm form whose meaning would change is rejected (docs/plans/math-library-plan.md,
// "Migration hazards"), so a migration slip is a compile error rather than a silent transpose.
template<typename M>
concept SingleIndexable = requires(M m) { m[0]; };
template<typename M>
concept ScalarConstructible = requires { M{ 1.0f }; };
template<typename M>
concept ConstructibleFromFourColumns = requires(Float4 v) { M{ v, v, v, v }; };
template<typename M>
concept Invertible = requires(M m) { math::inverse(m); };
template<typename M>
concept HasIdentity = requires { M::identity(); };
template<typename V, typename M>
concept RowVectorProduct = requires(V v, M m) { v * m; };
template<typename M, typename V>
concept MatrixVectorProduct = requires(M m, V v) { m * v; };
template<typename A, typename B>
concept Multipliable = requires(A a, B b) { a * b; };
template<typename A, typename B>
concept Divisible = requires(A a, B b) { a / b; };

static_assert(!SingleIndexable<Float4x4>, "glm's m[column] does not compile");
static_assert(!ScalarConstructible<Float4x4>, "glm's mat4{ 1.0f } does not compile");
static_assert(!ConstructibleFromFourColumns<Float4x4>, "glm's column list does not compile");
static_assert(!std::is_default_constructible_v<Float4x4>, "the state is always named");
static_assert(!std::is_default_constructible_v<Float3x4>);
static_assert(!Invertible<Float3x4>);
static_assert(!Invertible<math::Matrix<std::int32_t, 3, 3>>);
static_assert(Invertible<Float3x3>);
static_assert(!HasIdentity<Float3x4>);
static_assert(!RowVectorProduct<Float4, Float4x4>, "no row-vector product");
static_assert(!MatrixVectorProduct<Float4x4, Float3>);
static_assert(!Multipliable<Float4x4, Double4x4>);
static_assert(!Multipliable<Float3x4, Float3x4>, "3x4 times 3x4 has no inner dimension");
static_assert(Multipliable<Float3x4, Float4x3>);
static_assert(!Divisible<Float4x4, float>, "no matrix division");
static_assert(!std::is_convertible_v<Float4x4, Float3x4>, "shape conversion is explicit");
static_assert(!std::is_convertible_v<Double4x4, Float4x4>, "scalar conversion is explicit");
static_assert(math::MatrixType<Float2x3>);
static_assert(!math::MatrixType<Float4>);

// The types are usable in constant expressions.
static_assert(Float2x2::identity() * Float2x2::identity() == Float2x2::identity());
static_assert(math::transpose(Float2x3::zero()) == Float3x2::zero());
}

TEST_SUITE("math::Matrix")
{
    TEST_CASE_TEMPLATE("zero and identity name their state", M, AEGIS_ALL_MATRICES)
    {
        using T = typename M::ValueType;
        const auto zero = M::zero();
        for (std::size_t r = 0; r < M::rowCount; ++r)
        {
            for (std::size_t c = 0; c < M::columnCount; ++c)
                CHECK(zero[r, c] == T{ 0 });
        }
        if constexpr (M::rowCount == M::columnCount)
        {
            const auto identity = M::identity();
            for (std::size_t r = 0; r < M::rowCount; ++r)
            {
                for (std::size_t c = 0; c < M::columnCount; ++c)
                    CHECK(identity[r, c] == (r == c ? T{ 1 } : T{ 0 }));
            }
        }
    }

    TEST_CASE_TEMPLATE("reads and writes rows and columns", M, AEGIS_ALL_MATRICES)
    {
        using T = typename M::ValueType;
        auto m = indexed<M>();
        for (std::size_t i = 0; i < M::rowCount; ++i)
        {
            for (std::size_t c = 0; c < M::columnCount; ++c)
                CHECK(m.row(i)[c] == m[i, c]);
        }
        for (std::size_t i = 0; i < M::columnCount; ++i)
        {
            for (std::size_t r = 0; r < M::rowCount; ++r)
                CHECK(m.column(i)[r] == m[r, i]);
        }

        m.setRow(1, RowOf<M>{ T{ 7 } });
        CHECK(m.row(1) == RowOf<M>{ T{ 7 } });
        m.setColumn(0, ColumnOf<M>{ T{ 9 } });
        CHECK(m.column(0) == ColumnOf<M>{ T{ 9 } });
        CHECK(m[1, 0] == T{ 9 });
    }

    TEST_CASE_TEMPLATE("round-trips through fromRows and fromColumns", M, AEGIS_ALL_MATRICES)
    {
        const auto m = indexed<M>();
        if constexpr (M::rowCount == 2)
            CHECK(M::fromRows(m.row(0), m.row(1)) == m);
        else if constexpr (M::rowCount == 3)
            CHECK(M::fromRows(m.row(0), m.row(1), m.row(2)) == m);
        else
            CHECK(M::fromRows(m.row(0), m.row(1), m.row(2), m.row(3)) == m);

        if constexpr (M::columnCount == 2)
            CHECK(M::fromColumns(m.column(0), m.column(1)) == m);
        else if constexpr (M::columnCount == 3)
            CHECK(M::fromColumns(m.column(0), m.column(1), m.column(2)) == m);
        else
            CHECK(M::fromColumns(m.column(0), m.column(1), m.column(2), m.column(3)) == m);
    }

    TEST_CASE_TEMPLATE("transposes rows into columns", M, AEGIS_ALL_MATRICES)
    {
        const auto m = indexed<M>();
        const auto t = math::transpose(m);
        for (std::size_t i = 0; i < M::rowCount; ++i)
            CHECK(t.column(i) == m.row(i));
        CHECK(math::transpose(t) == m);
    }

    TEST_CASE_TEMPLATE("adds and scales element-wise", M, AEGIS_ALL_MATRICES)
    {
        using T = typename M::ValueType;
        const auto a = indexed<M>();
        const auto b = a * T{ 3 };
        const auto sum = a + b;
        const auto difference = b - a;
        for (std::size_t r = 0; r < M::rowCount; ++r)
        {
            for (std::size_t c = 0; c < M::columnCount; ++c)
            {
                CHECK(b[r, c] == a[r, c] * T{ 3 });
                CHECK(sum[r, c] == a[r, c] * T{ 4 });
                CHECK(difference[r, c] == a[r, c] * T{ 2 });
            }
        }
        CHECK(T{ 3 } * a == b);

        auto m = a;
        m += b;
        CHECK(m == sum);
        m -= b;
        CHECK(m == a);
        m *= T{ 3 };
        CHECK(m == b);
    }

    TEST_CASE_TEMPLATE("multiplies like glm", M, AEGIS_MATRICES)
    {
        using T = typename M::ValueType;
        using Transposed = math::Matrix<T, M::columnCount, M::rowCount>;
        const double tolerance = oracle::tolerance<T>;
        std::mt19937 rng{ 444 };
        for (int sample = 0; sample < 20; ++sample)
        {
            CAPTURE(sample);
            const auto a = oracle::randomMatrix<M>(rng);
            const auto b = oracle::randomMatrix<Transposed>(rng);
            const auto v = oracle::randomVector<RowOf<M>>(rng);
            const auto ga = oracle::toGlmMat(a);
            const auto gb = oracle::toGlmMat(b);

            oracle::checkMat(a * b, ga * gb, tolerance); // R x C times C x R
            oracle::checkMat(b * a, gb * ga, tolerance); // C x R times R x C
            oracle::checkVec(a * v, ga * oracle::toGlmVec(v), tolerance);
            CHECK(math::mul(a, b) == a * b);
            CHECK(math::mul(a, v) == a * v);
        }
    }

    TEST_CASE("composes column vectors right to left")
    {
        // Scale, then translate: the translation must not be scaled.
        auto translate = Float4x4::identity();
        translate.setColumn(3, Float4{ 1.0f, 2.0f, 3.0f, 1.0f });
        auto scale = Float4x4::identity();
        scale[0, 0] = 2.0f;
        scale[1, 1] = 2.0f;
        scale[2, 2] = 2.0f;
        CHECK((translate * scale) * Float4{ 1.0f, 1.0f, 1.0f, 1.0f } == Float4{ 3.0f, 4.0f, 5.0f, 1.0f });
    }

    TEST_CASE("fromColumns matches glm's column constructor")
    {
        const Float4 c0{ 1.0f, 2.0f, 3.0f, 4.0f };
        const Float4 c1{ 5.0f, 6.0f, 7.0f, 8.0f };
        const Float4 c2{ 9.0f, 10.0f, 11.0f, 12.0f };
        const Float4 c3{ 13.0f, 14.0f, 15.0f, 16.0f };
        const glm::mat4 expected{ oracle::toGlmVec(c0), oracle::toGlmVec(c1), oracle::toGlmVec(c2), oracle::toGlmVec(c3) };
        oracle::checkMat(Float4x4::fromColumns(c0, c1, c2, c3), expected, 1e-12);
    }
}

TEST_SUITE("math::Matrix conversions")
{
    TEST_CASE("keeps the top rows of a 4x4 as a 3x4")
    {
        const auto m = indexed<Float4x4>();
        const Float3x4 top{ m };
        for (std::size_t r = 0; r < 3; ++r)
            CHECK(top.row(r) == m.row(r));
    }

    TEST_CASE("appends the identity row to a 3x4")
    {
        const auto m = indexed<Float3x4>();
        const Float4x4 full{ m };
        for (std::size_t r = 0; r < 3; ++r)
            CHECK(full.row(r) == m.row(r));
        CHECK(full.row(3) == Float4{ 0.0f, 0.0f, 0.0f, 1.0f });
    }

    TEST_CASE("keeps the upper-left block and fills the rest from identity")
    {
        const auto m = indexed<Float4x4>();
        const Float3x3 block{ m };
        CHECK(block == Float3x3::fromRows(Float3{ 0.0f, 1.0f, 2.0f }, Float3{ 10.0f, 11.0f, 12.0f }, Float3{ 20.0f, 21.0f, 22.0f }));

        const Float4x4 grown{ indexed<Float2x2>() };
        CHECK(grown.row(1) == Float4{ 10.0f, 11.0f, 0.0f, 0.0f });
        CHECK(grown.row(2) == Float4{ 0.0f, 0.0f, 1.0f, 0.0f });
        CHECK(grown.row(3) == Float4{ 0.0f, 0.0f, 0.0f, 1.0f });
    }

    TEST_CASE("converts the scalar type of each element")
    {
        auto m = Double4x4::identity();
        m[0, 3] = 1.0e6;
        m[2, 1] = -0.5;
        const Float4x4 converted{ m };
        CHECK(converted[0, 3] == 1.0e6f);
        CHECK(converted[2, 1] == -0.5f);
        CHECK(converted[3, 3] == 1.0f);
    }
}

TEST_SUITE("math::inverse")
{
    TEST_CASE_TEMPLATE("matches glm's determinant and inverse", M, AEGIS_SQUARE_MATRICES)
    {
        using T = typename M::ValueType;
        // The inverse amplifies rounding: compare in float with a looser bound than for products.
        const double tolerance = std::same_as<T, float> ? 1e-4 : 1e-10;
        std::mt19937 rng{ 444 };
        for (int sample = 0; sample < 50; ++sample)
        {
            CAPTURE(sample);
            const auto m = oracle::randomMatrix<M>(rng);
            const auto g = oracle::toGlmMat(m);
            // The determinant cancels terms of up to 10^N (elements in [-10, 10]): its error scales with them, not with it.
            const double termScale = std::pow(10.0, static_cast<double>(M::rowCount));
            CHECK(math::determinant(m) == doctest::Approx(glm::determinant(g)).epsilon(tolerance).scale(termScale));

            const auto invertible = randomInvertible<M>(rng);
            oracle::checkMat(math::inverse(invertible), glm::inverse(oracle::toGlmMat(invertible)), tolerance);
        }
    }

    TEST_CASE_TEMPLATE("multiplies with its matrix to identity", M, AEGIS_SQUARE_MATRICES)
    {
        using T = typename M::ValueType;
        const T tolerance = std::same_as<T, float> ? T(1e-4) : T(1e-10);
        std::mt19937 rng{ 7 };
        for (int sample = 0; sample < 50; ++sample)
        {
            CAPTURE(sample);
            const auto m = randomInvertible<M>(rng);
            CHECK(math::nearlyEqual(math::inverse(m) * m, M::identity(), tolerance));
            CHECK(math::nearlyEqual(m * math::inverse(m), M::identity(), tolerance));
        }
    }

    TEST_CASE("affineInverse equals inverse on affine input")
    {
        std::mt19937 rng{ 11 };
        for (int sample = 0; sample < 50; ++sample)
        {
            CAPTURE(sample);
            auto m4 = randomInvertible<Float4x4>(rng);
            m4.setRow(3, Float4{ 0.0f, 0.0f, 0.0f, 1.0f });
            const auto inverse4 = math::affineInverse(m4);
            CHECK(math::nearlyEqual(inverse4, math::inverse(m4), 1e-4f));
            CHECK(inverse4.row(3) == Float4{ 0.0f, 0.0f, 0.0f, 1.0f });

            auto m3 = randomInvertible<Double3x3>(rng);
            m3.setRow(2, math::Double3{ 0.0, 0.0, 1.0 });
            CHECK(math::nearlyEqual(math::affineInverse(m3), math::inverse(m3), 1e-10));
        }
    }

    TEST_CASE("nearlyEqual compares every element")
    {
        auto a = indexed<Float3x4>();
        auto b = a;
        b[2, 3] += 1e-6f;
        CHECK(math::nearlyEqual(a, b));
        b[0, 0] += 0.5f;
        CHECK_FALSE(math::nearlyEqual(a, b));
        CHECK(math::nearlyEqual(a, b, 0.5f));
    }
}
