#include <doctest/doctest.h>

import std;
import aegis.math;
import aegis.physics;
import aegis.world;

namespace
{
using namespace aegis;
using physics::BoxShape;
using physics::Collider;
using physics::RigidBody;
using physics::SphereShape;

// A playing world with physics and the given static and dynamic bodies.
struct Fixture
{
    world::World world;

    Fixture() { world.addSystem<physics::PhysicsSystem>(); }

    auto registry() -> world::Registry& { return world.registry(); }

    auto addBox(const math::Float3& location, const math::Float3& halfExtents, std::optional<RigidBody> body = std::nullopt) -> world::Entity
    {
        const auto entity = registry().create("Box", location);
        registry().add<Collider>(entity, Collider{ .shape = BoxShape{ .halfExtents = halfExtents } });
        if (body)
            registry().add<RigidBody>(entity, *body);
        return entity;
    }

    auto addBall(const math::Float3& location, float radius) -> world::Entity
    {
        const auto entity = registry().create("Ball", location);
        registry().add<Collider>(entity, Collider{ .shape = SphereShape{ .radius = radius } });
        return entity;
    }
};

auto near(const math::Float3& a, const math::Float3& b, float tolerance) -> bool
{
    return math::length(a - b) <= tolerance;
}

auto sorted(std::vector<world::Entity> entities) -> std::vector<world::Entity>
{
    std::ranges::sort(entities, [](world::Entity a, world::Entity b) { return std::to_underlying(a.id()) < std::to_underlying(b.id()); });
    return entities;
}
}

TEST_SUITE("physics::raycast")
{
    TEST_CASE("hits a static box below at the distance to its top with its top's normal")
    {
        Fixture fixture;
        const auto box = fixture.addBox({ 1.0f, 2.0f, -0.5f }, { 2.0f, 2.0f, 0.5f }); // Top at z = 0
        fixture.world.begin();

        const auto hit = physics::raycast(fixture.registry(), { 1.5f, 2.5f, 5.0f }, { 0.0f, 0.0f, -2.0f }, 100.0f);
        REQUIRE(hit.has_value());
        CHECK(hit->entity == box);
        CHECK(hit->distance == doctest::Approx(5.0f).epsilon(1.0e-4));
        CHECK(near(hit->point, { 1.5f, 2.5f, 0.0f }, 1.0e-3f));
        CHECK(near(hit->normal, math::world::up<float>, 1.0e-4f));
    }

    TEST_CASE("misses a box beyond the maximum distance or behind the origin")
    {
        Fixture fixture;
        fixture.addBox({ 0.0f, 0.0f, -0.5f }, { 2.0f, 2.0f, 0.5f });
        fixture.world.begin();

        CHECK_FALSE(physics::raycast(fixture.registry(), { 0.0f, 0.0f, 5.0f }, { 0.0f, 0.0f, -1.0f }, 4.9f).has_value());
        CHECK_FALSE(physics::raycast(fixture.registry(), { 0.0f, 0.0f, 5.0f }, { 0.0f, 0.0f, 1.0f }, 100.0f).has_value());
        CHECK_FALSE(physics::raycast(fixture.registry(), { 0.0f, 0.0f, 5.0f }, math::Float3{ 0.0f }, 100.0f).has_value());
    }

    TEST_CASE("passes through the ignored entity to the body behind it")
    {
        Fixture fixture;
        const auto floor = fixture.addBox({ 0.0f, 0.0f, -0.5f }, { 2.0f, 2.0f, 0.5f });
        const auto ball = fixture.addBall({ 0.0f, 0.0f, 2.0f }, 0.5f);
        fixture.world.begin();

        const auto first = physics::raycast(fixture.registry(), { 0.0f, 0.0f, 5.0f }, { 0.0f, 0.0f, -1.0f }, 100.0f);
        REQUIRE(first.has_value());
        CHECK(first->entity == ball);
        CHECK(first->distance == doctest::Approx(2.5f).epsilon(1.0e-4));

        const auto past = physics::raycast(fixture.registry(), { 0.0f, 0.0f, 5.0f }, { 0.0f, 0.0f, -1.0f }, 100.0f, ball);
        REQUIRE(past.has_value());
        CHECK(past->entity == floor);
    }

    TEST_CASE("finds nothing before the world begins or without a PhysicsSystem")
    {
        Fixture fixture;
        fixture.addBox({ 0.0f, 0.0f, -0.5f }, { 2.0f, 2.0f, 0.5f });
        CHECK_FALSE(physics::raycast(fixture.registry(), { 0.0f, 0.0f, 5.0f }, { 0.0f, 0.0f, -1.0f }, 100.0f).has_value());

        world::World bare;
        CHECK_FALSE(physics::raycast(bare.registry(), { 0.0f, 0.0f, 5.0f }, { 0.0f, 0.0f, -1.0f }, 100.0f).has_value());
        CHECK(physics::overlapSphere(bare.registry(), math::Float3{ 0.0f }, 10.0f).empty());
    }
}

TEST_SUITE("physics::overlapSphere and physics::overlapBox")
{
    TEST_CASE("finds exactly the bodies inside the volume")
    {
        Fixture fixture;
        const auto inside = fixture.addBox({ 1.0f, 0.0f, 0.0f }, math::Float3{ 0.25f });
        const auto across = fixture.addBall({ -2.0f, 0.0f, 0.0f }, 0.5f); // Reaches into the volume
        fixture.addBox({ 4.0f, 0.0f, 0.0f }, math::Float3{ 0.25f });      // Outside
        fixture.addBall({ 0.0f, 0.0f, 3.0f }, 0.5f);                      // Outside
        const auto dynamic = fixture.addBox({ 0.0f, 1.0f, 0.0f }, math::Float3{ 0.25f }, RigidBody{ .gravityFactor = 0.0f });
        fixture.world.begin();

        const auto expected = sorted({ inside, across, dynamic });
        CHECK(physics::overlapSphere(fixture.registry(), math::Float3{ 0.0f }, 2.0f) == expected);
        CHECK(physics::overlapBox(fixture.registry(), math::Float3{ 0.0f }, math::Float3{ 1.75f }, math::Quat::identity()) == expected);
    }

    TEST_CASE("turns the box by its rotation")
    {
        Fixture fixture;
        const auto ball = fixture.addBall({ 1.5f, 1.5f, 0.0f }, 0.1f);
        fixture.world.begin();

        // A long thin box along X misses the ball; turned an eighth around up it points through it.
        CHECK(physics::overlapBox(fixture.registry(), math::Float3{ 0.0f }, { 3.0f, 0.2f, 0.2f }, math::Quat::identity()).empty());
        const auto turned = math::Quat::fromAxisAngle(math::world::up<float>, std::numbers::pi_v<float> / 4.0f);
        CHECK(physics::overlapBox(fixture.registry(), math::Float3{ 0.0f }, { 3.0f, 0.2f, 0.2f }, turned) == std::vector{ ball });
    }

    TEST_CASE("leaves out the ignored entity")
    {
        Fixture fixture;
        const auto self = fixture.addBall(math::Float3{ 0.0f }, 0.5f);
        const auto other = fixture.addBall({ 1.0f, 0.0f, 0.0f }, 0.5f);
        fixture.world.begin();

        CHECK(physics::overlapSphere(fixture.registry(), math::Float3{ 0.0f }, 1.0f, self) == std::vector{ other });
    }
}
