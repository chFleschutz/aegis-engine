#include "glm_oracle.h"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

import std;
import aegis.math;

namespace math = aegis::math;
using math::Float3;
using math::Float4x4;
using math::Quat;
using DoubleQuat = math::Quaternion<double>;

TYPE_TO_STRING_AS("float", float);
TYPE_TO_STRING_AS("double", double);

namespace
{
template<typename T, typename Rng>
auto randomRotation(Rng& rng) -> math::Quaternion<T>
{
    const auto axis = math::normalize(oracle::randomVector<math::Vector<T, 3>>(rng));
    return math::Quaternion<T>::fromAxisAngle(axis, oracle::randomScalar<T>(rng));
}

/// @brief A scale with every component at least 0.1 away from zero, of random sign
template<typename T, typename Rng>
auto randomScale(Rng& rng) -> math::Vector<T, 3>
{
    auto scale = oracle::randomVector<math::Vector<T, 3>>(rng);
    for (unsigned i = 0; i < 3; ++i)
    {
        if (math::abs(scale[i]) < T{ 0.1 })
            scale[i] = T{ 1 };
    }
    return scale;
}

/// @brief The same projection with y flipped, as the engine's builders produce it
template<typename G>
auto flipY(G projection) -> G
{
    projection[1][1] = -projection[1][1];
    return projection;
}

// The axes and directions are usable in constant expressions.
static_assert(math::world::right<float> == Float3{ 1.0f, 0.0f, 0.0f });
static_assert(math::world::forward<float> == Float3{ 0.0f, 1.0f, 0.0f });
static_assert(math::world::up<float> == Float3{ 0.0f, 0.0f, 1.0f });
static_assert(math::forward(Quat::identity()) == math::world::forward<float>);
static_assert(math::transformMatrix(Float3{}, Quat::identity(), Float3{ 1.0f }) == Float4x4::identity());
}

TEST_SUITE("math::transform")
{
    TEST_CASE_TEMPLATE("an identity rotation yields the world axes", T, float, double)
    {
        const auto identity = math::Quaternion<T>::identity();
        CHECK(math::forward(identity) == math::world::forward<T>);
        CHECK(math::right(identity) == math::world::right<T>);
        CHECK(math::up(identity) == math::world::up<T>);
    }

    TEST_CASE_TEMPLATE("transformMatrix matches glm's translate * rotate * scale", T, float, double)
    {
        using V3 = math::Vector<T, 3>;
        using G4 = glm::mat<4, 4, T>;
        const double tolerance = oracle::tolerance<T> * 10.0;
        std::mt19937 rng{ 445 };
        for (int sample = 0; sample < 50; ++sample)
        {
            CAPTURE(sample);
            const V3 location = oracle::randomVector<V3>(rng);
            const auto rotation = randomRotation<T>(rng);
            const V3 scale = randomScale<T>(rng);

            const G4 expected = glm::translate(G4{ T{ 1 } }, oracle::toGlmVec(location)) * glm::mat4_cast(oracle::toGlmQuat(rotation)) *
                                glm::scale(G4{ T{ 1 } }, oracle::toGlmVec(scale));
            oracle::checkMat(math::transformMatrix(location, rotation, scale), expected, tolerance);
        }
    }
}

TEST_SUITE("math::decomposeTRS")
{
    TEST_CASE_TEMPLATE("round-trips random transforms, mirrored ones included", T, float, double)
    {
        using V3 = math::Vector<T, 3>;
        const double tolerance = sizeof(T) == sizeof(float) ? 1e-4 : 1e-10;
        std::mt19937 rng{ 445 };
        for (int sample = 0; sample < 100; ++sample)
        {
            CAPTURE(sample);
            const auto m = math::transformMatrix(oracle::randomVector<V3>(rng), randomRotation<T>(rng), randomScale<T>(rng));
            const auto [location, rotation, scale] = math::decomposeTRS(m);
            CHECK(math::length(rotation) == oracle::near(1.0, tolerance));
            oracle::checkMat(math::transformMatrix(location, rotation, scale), oracle::toGlmMat(m), tolerance);
        }
    }

    TEST_CASE("keeps a mirror as a negative x scale")
    {
        const Quat rotation = Quat::fromAxisAngle(math::world::up<float>, math::radians(30.0f)) *
                              Quat::fromAxisAngle(math::world::right<float>, math::radians(50.0f));
        const Float3 location{ 1.0f, -2.0f, 3.0f };

        SUBCASE("mirrored x axis")
        {
            const auto [t, q, s] = math::decomposeTRS(math::transformMatrix(location, rotation, Float3{ -1.0f, 2.0f, 1.0f }));
            oracle::checkVec(s, glm::vec<3, float>{ -1.0f, 2.0f, 1.0f }, 1e-5);
            oracle::checkQuat(q, oracle::toGlmQuat(rotation), 1e-5);
        }
        SUBCASE("mirrored y axis")
        {
            const auto [t, q, s] = math::decomposeTRS(math::transformMatrix(location, rotation, Float3{ 1.5f, -2.0f, 1.0f }));
            CHECK(s.x < 0.0f);
            CHECK(s.y > 0.0f);
            CHECK(s.z > 0.0f);
        }
    }

    TEST_CASE("a zero scale gives the identity rotation and no NaN")
    {
        const Quat rotation = Quat::fromAxisAngle(math::world::up<float>, math::radians(30.0f));
        const Float3 location{ 1.0f, -2.0f, 3.0f };
        const auto [t, q, s] = math::decomposeTRS(math::transformMatrix(location, rotation, Float3{ 0.0f, 2.0f, 1.0f }));
        CHECK(t == location);
        CHECK(q == Quat::identity());
        CHECK(s.x == 0.0f);
        CHECK(s.y == oracle::near(2.0, 1e-5));
        CHECK(s.z == oracle::near(1.0, 1e-5));
    }
}

TEST_SUITE("math::viewMatrix")
{
    TEST_CASE_TEMPLATE("matches glm's lookAtRH and its inverse", T, float, double)
    {
        using V3 = math::Vector<T, 3>;
        const double tolerance = oracle::tolerance<T> * 100.0; // glm's inverse on top
        std::mt19937 rng{ 445 };
        for (int sample = 0; sample < 50; ++sample)
        {
            CAPTURE(sample);
            const V3 location = oracle::randomVector<V3>(rng);
            V3 forward = oracle::randomVector<V3>(rng);
            if (math::length(math::cross(math::normalize(forward), math::world::up<T>)) < T{ 0.1 })
                forward = math::world::forward<T>;

            const auto g = glm::lookAtRH(oracle::toGlmVec(location), oracle::toGlmVec(location + forward), oracle::toGlmVec(math::world::up<T>));
            oracle::checkMat(math::viewMatrix(location, forward), g, tolerance);
            oracle::checkMat(math::inverseViewMatrix(location, forward), glm::inverse(g), tolerance);
        }
    }

    TEST_CASE_TEMPLATE("the inverse view times the view is the identity", T, float, double)
    {
        using V3 = math::Vector<T, 3>;
        const V3 location{ T{ 3 }, T{ -4 }, T{ 2.5 } };
        const V3 forward{ T{ 0.3 }, T{ 1 }, T{ -0.2 } };
        const auto view = math::viewMatrix(location, forward);
        const auto inverseView = math::inverseViewMatrix(location, forward);
        const T tolerance = T(oracle::tolerance<T> * 10.0);
        CHECK(math::nearlyEqual(view * inverseView, math::Matrix<T, 4, 4>::identity(), tolerance));
        CHECK(math::nearlyEqual(inverseView * view, math::Matrix<T, 4, 4>::identity(), tolerance));
    }
}

TEST_SUITE("math::projection")
{
    TEST_CASE_TEMPLATE("perspective is glm's perspectiveRH_ZO with y flipped", T, float, double)
    {
        const double tolerance = oracle::tolerance<T> * 10.0;
        std::mt19937 rng{ 445 };
        for (int sample = 0; sample < 50; ++sample)
        {
            CAPTURE(sample);
            const T fovY = math::radians(T{ 20 } + math::abs(oracle::randomScalar<T>(rng)) * T{ 10 }); // 20..120 degrees
            const T aspect = T{ 0.25 } + math::abs(oracle::randomScalar<T>(rng)) / T{ 2 };
            const T near = T{ 0.01 } + math::abs(oracle::randomScalar<T>(rng));
            const T far = near + T{ 1 } + math::abs(oracle::randomScalar<T>(rng)) * T{ 100 };
            oracle::checkMat(math::perspective<T>(fovY, aspect, near, far), flipY(glm::perspectiveRH_ZO(fovY, aspect, near, far)), tolerance);
        }
    }

    TEST_CASE_TEMPLATE("orthographic is glm's orthoRH_ZO of a centred box with y flipped", T, float, double)
    {
        const double tolerance = oracle::tolerance<T> * 10.0;
        std::mt19937 rng{ 445 };
        for (int sample = 0; sample < 50; ++sample)
        {
            CAPTURE(sample);
            const T halfWidth = T{ 0.1 } + math::abs(oracle::randomScalar<T>(rng));
            const T halfHeight = T{ 0.1 } + math::abs(oracle::randomScalar<T>(rng));
            const T near = oracle::randomScalar<T>(rng);
            const T far = near + T{ 1 } + math::abs(oracle::randomScalar<T>(rng)) * T{ 10 };
            oracle::checkMat(math::orthographic<T>(halfWidth, halfHeight, near, far),
                flipY(glm::orthoRH_ZO(-halfWidth, halfWidth, -halfHeight, halfHeight, near, far)), tolerance);
        }
    }

    TEST_CASE("perspective maps near to depth 0, far to depth 1 and up to negative y")
    {
        const float near = 0.1f;
        const float far = 100.0f;
        const auto projection = math::perspective<float>(math::radians(60.0f), 16.0f / 9.0f, near, far);
        const auto ndc = [&](Float3 view) {
            const auto clip = projection * math::Float4{ view, 1.0f };
            return Float3{ clip } / clip.w;
        };
        CHECK(ndc(Float3{ 0.0f, 0.0f, -near }).z == oracle::near(0.0, 1e-5));
        CHECK(ndc(Float3{ 0.0f, 0.0f, -far }).z == oracle::near(1.0, 1e-5));
        CHECK(ndc(Float3{ 0.0f, 1.0f, -1.0f }).y < 0.0f);
    }

    TEST_CASE("orthographic matches the sun shadow's hand-written projection")
    {
        const float halfWidth = 12.5f;
        const float depth = 80.0f;
        auto expected = Float4x4::identity();
        expected[0, 0] = 1.0f / halfWidth;
        expected[1, 1] = -1.0f / halfWidth;
        expected[2, 2] = -1.0f / depth;
        expected[2, 3] = 0.0f;
        const auto projection = math::orthographic<float>(halfWidth, halfWidth, 0.0f, depth);
        CHECK(projection == expected);
        // +0, not -0, so the sun shadow projection keeps its bits when it moves onto the builder
        CHECK_FALSE(std::signbit(projection[2, 3]));
    }
}
