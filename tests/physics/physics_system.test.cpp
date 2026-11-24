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
using physics::MotionType;
using physics::RigidBody;
using physics::SphereShape;

constexpr float STEP{ world::FixedStep::DEFAULT_STEP_SEC };

// A playing world with the engine's order of systems: physics, then the transforms.
struct Fixture
{
    world::World world;

    Fixture()
    {
        world.addSystem<physics::PhysicsSystem>();
        world.addSystem<world::TransformSystem>();
    }

    auto registry() -> world::Registry& { return world.registry(); }
    auto system() -> physics::PhysicsSystem& { return *registry().context<physics::PhysicsSystem>(); }

    auto addBox(const math::Float3& location, const math::Float3& halfExtents, std::optional<RigidBody> body) -> world::Entity
    {
        const auto entity = registry().create("Box", location);
        registry().add<Collider>(entity, Collider{ .shape = BoxShape{ .halfExtents = halfExtents } });
        if (body)
            registry().add<RigidBody>(entity, *body);
        return entity;
    }

    // A static floor whose top is at z = 0.
    auto addFloor() -> world::Entity { return addBox({ 0.0f, 0.0f, -0.5f }, { 10.0f, 10.0f, 0.5f }, std::nullopt); }

    // Updates the world 'frames' times by one fixed step each.
    void run(int frames)
    {
        for (int frame = 0; frame < frames; ++frame)
            world.update(STEP);
    }

    auto location(world::Entity entity) -> math::Float3 { return registry().get<Transform>(entity).location; }
};

auto near(const math::Float3& a, const math::Float3& b, float tolerance) -> bool
{
    return math::length(a - b) <= tolerance;
}
}

TEST_SUITE("physics::PhysicsSystem")
{
    TEST_CASE("rests a dynamic box dropped on a static box on top of it")
    {
        Fixture fixture;
        fixture.addFloor();
        const auto box = fixture.addBox({ 0.0f, 0.0f, 3.0f }, math::Float3{ 0.5f }, RigidBody{});
        fixture.world.begin();
        fixture.run(240);

        const auto rested = fixture.location(box);
        CHECK(std::abs(rested.z - 0.5f) < 0.005f);
        CHECK(std::abs(rested.x) < 0.005f);
        CHECK(std::abs(rested.y) < 0.005f);
        // Its global transform follows within the frame: the TransformSystem runs after the write.
        CHECK(near(fixture.registry().get<GlobalTransform>(box).location(), rested, 1.0e-5f));
        CHECK(fixture.registry().has<DynamicTag>(box));
    }

    TEST_CASE("falls with the world's gravity along the engine's down axis")
    {
        Fixture fixture;
        const auto ball = fixture.registry().create("Ball", { 0.0f, 0.0f, 10.0f });
        fixture.registry().add<Collider>(ball, Collider{ .shape = SphereShape{} });
        fixture.registry().add<RigidBody>(ball, RigidBody{ .linearDamping = 0.0f });
        fixture.world.begin();
        CHECK(fixture.system().gravity().z == doctest::Approx(-9.81f));
        fixture.run(60);

        // At alpha 0 the Transform shows the step before the last: 59 steps of 1/60 s of semi-implicit Euler,
        // 9.81 * (1 + 2 + ... + 59) / 3600
        const auto fallen = 10.0f - fixture.location(ball).z;
        CHECK(fallen == doctest::Approx(9.81f * 1770.0f / 3600.0f).epsilon(0.001));
        CHECK(std::abs(fixture.location(ball).x) < 1.0e-5f);
    }

    TEST_CASE("moves a kinematic body with its entity")
    {
        // A dynamic box on a kinematic platform that rises by one metre in a second ends one metre higher.
        Fixture fixture;
        const auto platform = fixture.addBox({ 0.0f, 0.0f, -0.5f }, { 2.0f, 2.0f, 0.5f }, RigidBody{ .motionType = MotionType::Kinematic });
        const auto box = fixture.addBox({ 0.0f, 0.0f, 0.5f }, math::Float3{ 0.5f }, RigidBody{});
        fixture.world.begin();
        CHECK(fixture.registry().has<DynamicTag>(platform));
        fixture.run(30);
        const auto before = fixture.location(box).z;

        for (int frame = 1; frame <= 60; ++frame)
        {
            fixture.registry().get<Transform>(platform).location.z = -0.5f + static_cast<float>(frame) / 60.0f;
            fixture.world.update(STEP);
        }
        fixture.run(60);

        CHECK(fixture.location(platform).z == doctest::Approx(0.5f)); // The system leaves a kinematic Transform alone
        CHECK(std::abs(fixture.location(box).z - before - 1.0f) < 0.01f);
    }

    TEST_CASE("writes a parented body's pose in its parent's space")
    {
        Fixture fixture;
        // Turned a quarter around X: the child's local Y points down the world's Z, so its fall shows on local Y.
        const auto parent = fixture.registry().create("Parent", { 10.0f, 0.0f, 0.0f }, math::Quat::fromAxisAngle(math::world::right<float>, std::numbers::pi_v<float> / 2.0f));
        const auto child = fixture.registry().create("Child", { 1.0f, 0.0f, 0.0f });
        REQUIRE(fixture.registry().setParent(child, parent).has_value());
        fixture.registry().add<Collider>(child, Collider{ .shape = SphereShape{} });
        fixture.registry().add<RigidBody>(child, RigidBody{});
        fixture.world.begin();
        fixture.run(30);

        const auto local = fixture.location(child);
        const auto global = fixture.registry().get<GlobalTransform>(child).location();
        CHECK(global.x == doctest::Approx(11.0f));
        CHECK(std::abs(global.y) < 1.0e-4f);
        CHECK(global.z < -1.0f);
        CHECK(local.x == doctest::Approx(1.0f));
        CHECK(local.y == doctest::Approx(global.z).epsilon(1.0e-4));
        CHECK(std::abs(local.z) < 1.0e-4f);
        CHECK(fixture.registry().get<Transform>(child).scale == math::Float3{ 1.0f }); // Its scale stays its own
    }

    TEST_CASE("writes the pose halfway between two steps at alpha 0.5")
    {
        // Two worlds alike, one updated by whole steps and the other by one step and then a step and a half. At alpha 0
        // a frame shows the step before the last, so the whole-step world's second and third frames show steps one and two.
        auto drop = [](Fixture& fixture) {
            const auto box = fixture.addBox({ 0.0f, 0.0f, 5.0f }, math::Float3{ 0.5f }, RigidBody{});
            fixture.registry().get<Transform>(box).rotation = math::Quat::fromAxisAngle(math::world::forward<float>, 0.3f);
            fixture.registry().get<RigidBody>(box).angularDamping = 0.0f;
            fixture.world.begin();
            return box;
        };
        Fixture whole;
        const auto wholeBox = drop(whole);
        whole.world.update(STEP);
        CHECK(whole.registry().get<Transform>(wholeBox).location.z == 5.0f); // Step zero: where it began
        whole.world.update(STEP);
        const auto first = whole.registry().get<Transform>(wholeBox);
        whole.world.update(STEP);
        const auto second = whole.registry().get<Transform>(wholeBox);
        REQUIRE(second.location.z < first.location.z);

        Fixture blended;
        const auto blendedBox = drop(blended);
        blended.world.update(STEP);
        blended.world.update(1.5f * STEP);
        REQUIRE(blended.world.time().alpha == doctest::Approx(0.5f));

        const auto& halfway = blended.registry().get<Transform>(blendedBox);
        CHECK(near(halfway.location, math::lerp(first.location, second.location, 0.5f), 1.0e-5f));
        const auto expected = math::slerp(first.rotation, second.rotation, 0.5f);
        CHECK(std::abs(math::dot(halfway.rotation, expected)) == doctest::Approx(1.0f).epsilon(1.0e-5));
    }

    TEST_CASE("makes an entity with a Collider and no RigidBody a static body")
    {
        Fixture fixture;
        const auto wall = fixture.addBox({ 0.0f, 0.0f, 4.0f }, math::Float3{ 1.0f }, std::nullopt);
        fixture.world.begin();
        fixture.run(30);
        CHECK(fixture.system().hasBody(wall));
        CHECK(fixture.location(wall) == math::Float3{ 0.0f, 0.0f, 4.0f });
        CHECK_FALSE(fixture.registry().has<DynamicTag>(wall));
    }

    TEST_CASE("gives no body to a RigidBody without a Collider")
    {
        Fixture fixture;
        const auto entity = fixture.registry().create("Body", { 0.0f, 0.0f, 4.0f });
        fixture.registry().add<RigidBody>(entity);
        fixture.world.begin();
        fixture.run(10);
        CHECK_FALSE(fixture.system().hasBody(entity));
        CHECK(fixture.location(entity).z == 4.0f);

        // A Collider added later gives it its body at the next step.
        fixture.registry().add<Collider>(entity);
        fixture.run(10);
        CHECK(fixture.system().hasBody(entity));
        CHECK(fixture.location(entity).z < 4.0f);
    }

    TEST_CASE("follows bodies added and removed after begin")
    {
        Fixture fixture;
        fixture.addFloor();
        fixture.world.begin();
        CHECK(fixture.system().bodyCount() == 1);

        const auto box = fixture.addBox({ 0.0f, 0.0f, 2.0f }, math::Float3{ 0.5f }, RigidBody{});
        fixture.run(1);
        CHECK(fixture.system().bodyCount() == 2);

        fixture.registry().remove<RigidBody>(box); // Becomes static where it is
        fixture.run(1);
        const auto stopped = fixture.location(box);
        fixture.run(30);
        CHECK(fixture.location(box) == stopped);

        fixture.registry().destroy(box);
        CHECK(fixture.system().bodyCount() == 1);
    }

    TEST_CASE("scales the shape by the entity's global scale")
    {
        Fixture fixture;
        fixture.addFloor();
        const auto box = fixture.addBox({ 0.0f, 0.0f, 3.0f }, math::Float3{ 0.5f }, RigidBody{});
        fixture.registry().get<Transform>(box).scale = math::Float3{ 2.0f };
        fixture.world.begin();
        fixture.run(240);
        CHECK(std::abs(fixture.location(box).z - 1.0f) < 0.005f);
    }

    TEST_CASE("leaves the bodies alone in edit mode")
    {
        Fixture fixture;
        const auto box = fixture.addBox({ 0.0f, 0.0f, 3.0f }, math::Float3{ 0.5f }, RigidBody{});
        fixture.world.begin(world::UpdateMode::Edit);
        for (int frame = 0; frame < 30; ++frame)
            fixture.world.update(STEP, world::UpdateMode::Edit);
        CHECK(fixture.system().bodyCount() == 0);
        CHECK(fixture.location(box).z == 3.0f);
    }

    TEST_CASE("removes every body when the world is reset")
    {
        Fixture fixture;
        fixture.addFloor();
        fixture.world.begin();
        REQUIRE(fixture.system().bodyCount() == 1);
        auto& system = fixture.system();
        fixture.registry().clear(); // What World::reset does, before the systems go
        CHECK(system.bodyCount() == 0);
    }
}
