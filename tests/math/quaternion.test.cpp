#include "glm_oracle.h"

import std;
import aegis.math;

namespace math = aegis::math;
using math::Float3;
using math::Float3x3;
using math::Quat;
using DoubleQuat = math::Quaternion<double>;

TYPE_TO_STRING_AS("Quat", Quat);
TYPE_TO_STRING_AS("Quaternion<double>", DoubleQuat);

namespace
{
template<typename Q, typename Rng>
auto randomRotation(Rng& rng) -> Q
{
    using T = typename Q::ValueType;
    const auto axis = math::normalize(oracle::randomVector<math::Vector<T, 3>>(rng));
    return Q::fromAxisAngle(axis, oracle::randomScalar<T>(rng));
}

// Compile-only checks: neither glm's positional constructor nor an implicit default compiles.
template<typename Q>
concept ConstructibleFromFourScalars = requires { Q{ 1.0f, 0.0f, 0.0f, 0.0f }; };
template<typename T>
concept QuaternionOf = requires { typename math::Quaternion<T>; math::Quaternion<T>::identity(); };

static_assert(!std::is_default_constructible_v<Quat>, "the state is always named");
static_assert(!ConstructibleFromFourScalars<Quat>, "glm's quat{ w, x, y, z } does not compile");
static_assert(!QuaternionOf<int>);
static_assert(QuaternionOf<double>);

// The types are usable in constant expressions.
static_assert(Quat::identity() * Quat::identity() == Quat::identity());
static_assert(Quat::identity() * Float3{ 1.0f, 2.0f, 3.0f } == Float3{ 1.0f, 2.0f, 3.0f });
}

TEST_SUITE("math::Quaternion")
{
    TEST_CASE("stores x y z w in that order")
    {
        const auto q = Quat::fromXyzw(1.0f, 2.0f, 3.0f, 4.0f);
        CHECK(q.x == 1.0f);
        CHECK(q.w == 4.0f);
        CHECK(std::bit_cast<std::array<float, 4>>(q) == std::array{ 1.0f, 2.0f, 3.0f, 4.0f });
        CHECK(Quat::identity() == Quat::fromXyzw(0.0f, 0.0f, 0.0f, 1.0f));
    }

    TEST_CASE_TEMPLATE("matches glm on random rotations", Q, Quat, DoubleQuat)
    {
        using T = typename Q::ValueType;
        using V3 = math::Vector<T, 3>;
        const double tolerance = oracle::tolerance<T> * 10.0; // trigonometry on top of the products
        std::mt19937 rng{ 444 };
        for (int sample = 0; sample < 50; ++sample)
        {
            CAPTURE(sample);
            const auto axis = math::normalize(oracle::randomVector<V3>(rng));
            const T angle = oracle::randomScalar<T>(rng);
            const Q a = Q::fromAxisAngle(axis, angle);
            const Q b = randomRotation<Q>(rng);
            const V3 v = oracle::randomVector<V3>(rng);
            const T t = math::abs(oracle::randomScalar<T>(rng)) / T{ 10 };
            const auto ga = oracle::toGlmQuat(a);
            const auto gb = oracle::toGlmQuat(b);

            oracle::checkQuat(a, glm::angleAxis(angle, oracle::toGlmVec(axis)), tolerance);
            oracle::checkQuat(a * b, ga * gb, tolerance);
            oracle::checkVec(a * v, ga * oracle::toGlmVec(v), tolerance);
            oracle::checkQuat(math::conjugate(a), glm::conjugate(ga), tolerance);
            oracle::checkQuat(math::inverse(a), glm::inverse(ga), tolerance);
            CHECK(math::dot(a, b) == oracle::near(glm::dot(ga, gb), tolerance));
            CHECK(math::length(a) == oracle::near(glm::length(ga), tolerance));
            oracle::checkQuat(math::slerp(a, b, t), glm::slerp(ga, gb, t), tolerance);
            oracle::checkMat(math::rotationMatrix(a), glm::mat3_cast(ga), tolerance);
            oracle::checkQuat(Q::fromMatrix(math::rotationMatrix(a)), glm::quat_cast(glm::mat3_cast(ga)), tolerance);

            const auto scaled = Q::fromXyzw(a.x * T{ 3 }, a.y * T{ 3 }, a.z * T{ 3 }, a.w * T{ 3 });
            oracle::checkQuat(math::normalize(scaled), glm::normalize(oracle::toGlmQuat(scaled)), tolerance);
        }
    }

    TEST_CASE_TEMPLATE("follows glm's Euler convention", Q, Quat, DoubleQuat)
    {
        using T = typename Q::ValueType;
        using V3 = math::Vector<T, 3>;
        const double tolerance = oracle::tolerance<T> * 10.0;
        std::mt19937 rng{ 1 };
        for (int sample = 0; sample < 50; ++sample)
        {
            CAPTURE(sample);
            const V3 euler = oracle::randomVector<V3>(rng) / T{ 4 };
            const Q q = Q::fromEuler(euler);
            const auto g = glm::qua<T>{ oracle::toGlmVec(euler) };
            oracle::checkQuat(q, g, tolerance);
            oracle::checkVec(math::eulerAngles(q), glm::eulerAngles(g), tolerance);

            // Within the principal ranges the angles round-trip.
            const V3 principal{ euler.x / T{ 2 }, euler.y / T{ 4 }, euler.z / T{ 2 } };
            CHECK(math::nearlyEqual(math::eulerAngles(Q::fromEuler(principal)), principal, T(tolerance)));
        }
    }

    TEST_CASE("rotates the world axes right-handed")
    {
        const Quat quarterTurnZ = Quat::fromAxisAngle(Float3{ 0.0f, 0.0f, 1.0f }, math::radians(90.0f));
        CHECK(math::nearlyEqual(quarterTurnZ * Float3{ 1.0f, 0.0f, 0.0f }, Float3{ 0.0f, 1.0f, 0.0f }));
        CHECK(math::nearlyEqual(math::rotationMatrix(quarterTurnZ) * Float3{ 1.0f, 0.0f, 0.0f }, Float3{ 0.0f, 1.0f, 0.0f }));
        CHECK(math::nearlyEqual(math::inverse(quarterTurnZ) * Float3{ 0.0f, 1.0f, 0.0f }, Float3{ 1.0f, 0.0f, 0.0f }));
    }

    TEST_CASE("round-trips through a matrix near a half turn")
    {
        // fromMatrix picks its pivot by the largest component, which changes near 180 degrees.
        std::mt19937 rng{ 3 };
        for (int sample = 0; sample < 50; ++sample)
        {
            CAPTURE(sample);
            const auto axis = math::normalize(oracle::randomVector<math::Double3>(rng));
            const double angle = math::pi<double> + oracle::randomScalar<double>(rng) * 1e-4;
            const auto q = DoubleQuat::fromAxisAngle(axis, angle);
            const auto back = DoubleQuat::fromMatrix(math::rotationMatrix(q));
            // q and -q are the same rotation
            const auto negated = DoubleQuat::fromXyzw(-back.x, -back.y, -back.z, -back.w);
            CHECK((math::nearlyEqual(back, q, 1e-9) || math::nearlyEqual(negated, q, 1e-9)));
            CHECK(math::nearlyEqual(math::rotationMatrix(back), math::rotationMatrix(q), 1e-9));
        }
    }

    TEST_CASE("normalizing the zero quaternion gives identity")
    {
        CHECK(math::normalize(Quat::fromXyzw(0.0f, 0.0f, 0.0f, 0.0f)) == Quat::identity());
    }

    TEST_CASE("slerp takes the shorter arc and is exact at its ends")
    {
        const Quat a = Quat::identity();
        const Quat b = Quat::fromAxisAngle(Float3{ 0.0f, 0.0f, 1.0f }, math::radians(90.0f));
        const Quat farB = Quat::fromXyzw(-b.x, -b.y, -b.z, -b.w); // the same rotation, the long way round
        CHECK(math::nearlyEqual(math::slerp(a, b, 0.0f), a));
        CHECK(math::nearlyEqual(math::slerp(a, b, 1.0f), b));
        const Quat half = Quat::fromAxisAngle(Float3{ 0.0f, 0.0f, 1.0f }, math::radians(45.0f));
        CHECK(math::nearlyEqual(math::slerp(a, b, 0.5f), half));
        CHECK(math::nearlyEqual(math::slerp(a, farB, 0.5f), half));
        CHECK(math::slerp(a, a, 0.5f) == a);
    }
}
