#pragma once

// glm as the test oracle for aegis.math's own types. Include this first, before any import.
//
// aegis.math still includes glm in its global module fragment with these same macros; they must agree, or the glm
// declarations seen through the import and through this header differ.
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <doctest/doctest.h>
#include <glm/glm.hpp>
#include <glm/gtc/packing.hpp>
#include <glm/gtc/quaternion.hpp>

// The helpers are templates over aegis.math's types, which this header cannot name: they read them through
// `componentCount`, `rowCount`/`columnCount`, `[i]`, `[r, c]` and `x y z w`.
namespace oracle
{
/// @brief A value in [-10, 10] on a 0.001 grid from a random bit generator: identical on every standard library,
/// unlike std::uniform_real_distribution
template<typename T, typename Rng>
auto randomScalar(Rng& rng) -> T
{
    return static_cast<T>(static_cast<double>(rng() % 20001u) / 1000.0 - 10.0);
}

template<typename V, typename Rng>
auto randomVector(Rng& rng) -> V
{
    V v;
    for (unsigned i = 0; i < V::componentCount; ++i)
        v[i] = randomScalar<typename V::ValueType>(rng);
    return v;
}

template<typename M, typename Rng>
auto randomMatrix(Rng& rng) -> M
{
    auto m = M::zero();
    for (unsigned r = 0; r < M::rowCount; ++r)
    {
        for (unsigned c = 0; c < M::columnCount; ++c)
            m[r, c] = randomScalar<typename M::ValueType>(rng);
    }
    return m;
}

template<typename V>
auto toGlmVec(const V& v)
{
    constexpr auto n = static_cast<glm::length_t>(V::componentCount);
    glm::vec<n, typename V::ValueType> g{};
    for (glm::length_t i = 0; i < n; ++i)
        g[i] = v[static_cast<unsigned>(i)];
    return g;
}

/// @brief The same matrix in glm's column-major form: `g[c][r] == m[r, c]`
template<typename M>
auto toGlmMat(const M& m)
{
    constexpr auto rows = static_cast<glm::length_t>(M::rowCount);
    constexpr auto columns = static_cast<glm::length_t>(M::columnCount);
    glm::mat<columns, rows, typename M::ValueType> g{};
    for (glm::length_t c = 0; c < columns; ++c)
    {
        for (glm::length_t r = 0; r < rows; ++r)
            g[c][r] = m[static_cast<unsigned>(r), static_cast<unsigned>(c)];
    }
    return g;
}

template<typename Q>
auto toGlmQuat(Q q)
{
    return glm::qua<typename Q::ValueType>::wxyz(q.w, q.x, q.y, q.z);
}

/// @brief Equal within `tolerance`, scaled by the larger magnitude once that exceeds 1 (as math::nearlyEqual)
inline auto near(double expected, double tolerance) -> doctest::Approx
{
    return doctest::Approx(expected).epsilon(tolerance).scale(1.0);
}

template<typename V, typename G>
void checkVec(const V& actual, const G& expected, double tolerance)
{
    for (unsigned i = 0; i < V::componentCount; ++i)
    {
        CAPTURE(i);
        CHECK(static_cast<double>(actual[i]) == near(static_cast<double>(expected[static_cast<glm::length_t>(i)]), tolerance));
    }
}

template<typename M, typename G>
void checkMat(const M& actual, const G& expected, double tolerance)
{
    for (unsigned r = 0; r < M::rowCount; ++r)
    {
        for (unsigned c = 0; c < M::columnCount; ++c)
        {
            CAPTURE(r);
            CAPTURE(c);
            const double glmElement = static_cast<double>(expected[static_cast<glm::length_t>(c)][static_cast<glm::length_t>(r)]);
            CHECK(static_cast<double>(actual[r, c]) == near(glmElement, tolerance));
        }
    }
}

template<typename Q, typename G>
void checkQuat(Q actual, const G& expected, double tolerance)
{
    CHECK(static_cast<double>(actual.x) == near(static_cast<double>(expected.x), tolerance));
    CHECK(static_cast<double>(actual.y) == near(static_cast<double>(expected.y), tolerance));
    CHECK(static_cast<double>(actual.z) == near(static_cast<double>(expected.z), tolerance));
    CHECK(static_cast<double>(actual.w) == near(static_cast<double>(expected.w), tolerance));
}

/// @brief The test tolerance for a scalar type: products in float agree with glm to about 1e-5 relative
template<typename T>
constexpr double tolerance = sizeof(T) == sizeof(float) ? 1e-5 : 1e-12;
}
