#include <doctest/doctest.h>

import std;
import aegis.math;
import aegis.physics;
import aegis.reflect;
import aegis.scripting;
import aegis.world;

namespace
{
using namespace aegis;
using physics::BoxShape;
using physics::Collider;
using physics::ContactBegin;
using physics::ContactEnd;
using physics::MotionType;
using physics::RigidBody;

constexpr float STEP{ world::FixedStep::DEFAULT_STEP_SEC };

// A playing world with the engine's order of systems, which keeps every contact event it emits.
struct Fixture
{
    world::World world;
    std::vector<ContactBegin> begins;
    std::vector<ContactEnd> ends;

    Fixture()
    {
        world.addSystem<physics::PhysicsSystem>();
        world.addSystem<world::TransformSystem>();
    }

    auto registry() -> world::Registry& { return world.registry(); }

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

    // Updates the world once by one fixed step and keeps the events it can read: the previous update's.
    void update()
    {
        world.update(STEP);
        std::ranges::copy(registry().events().read<ContactBegin>(), std::back_inserter(begins));
        std::ranges::copy(registry().events().read<ContactEnd>(), std::back_inserter(ends));
    }

    void run(int frames)
    {
        for (int frame = 0; frame < frames; ++frame)
            update();
    }
};

auto near(const math::Float3& a, const math::Float3& b, float tolerance) -> bool
{
    return math::length(a - b) <= tolerance;
}

// Counts the ContactBegin events that name its entity, per update.
struct ContactCounter : scripting::Script
{
    std::vector<std::size_t> counts;

    void onUpdate(scripting::ScriptContext& context, float) override
    {
        const auto self = context.entity();
        counts.push_back(static_cast<std::size_t>(std::ranges::count_if(context.events<ContactBegin>(), [self](const ContactBegin& contact) {
            return contact.a == self || contact.b == self;
        })));
    }
};
}

TEST_SUITE("physics::ContactBegin and physics::ContactEnd")
{
    TEST_CASE("sends one begin and no end for a box that falls onto the floor and sleeps there")
    {
        Fixture fixture;
        const auto floor = fixture.addFloor();
        const auto box = fixture.addBox({ 0.0f, 0.0f, 1.5f }, math::Float3{ 0.5f }, RigidBody{});
        fixture.world.begin();
        fixture.run(300); // Five seconds: it lands and falls asleep

        REQUIRE(fixture.begins.size() == 1);
        const auto& contact = fixture.begins.front();
        CHECK(contact.a == floor); // Created first: the lower ID
        CHECK(contact.b == box);
        CHECK(near(contact.normal, math::world::up<float>, 1.0e-3f));
        CHECK(std::abs(contact.point.z) < 0.05f);
        CHECK(std::abs(contact.point.x) < 0.5f);
        CHECK(fixture.ends.empty());
    }

    TEST_CASE("sends one begin and one end for two boxes that touch and part")
    {
        Fixture fixture;
        const auto paddle = fixture.addBox({ -2.0f, 0.0f, 0.0f }, math::Float3{ 0.5f }, RigidBody{ .motionType = MotionType::Kinematic });
        const auto box = fixture.addBox(math::Float3{ 0.0f }, math::Float3{ 0.5f }, RigidBody{ .linearDamping = 0.0f, .gravityFactor = 0.0f });
        fixture.world.begin();

        // The paddle moves 1.5 m along X at 1 m/s, hitting the box after 1 m and pushing it, then stops; the box
        // slides on and away from it.
        for (int frame = 1; frame <= 90; ++frame)
        {
            fixture.registry().get<Transform>(paddle).location.x = -2.0f + static_cast<float>(frame) / 60.0f;
            fixture.update();
        }
        CHECK(fixture.begins.size() == 1);
        CHECK(fixture.ends.empty());
        fixture.run(120);

        REQUIRE(fixture.begins.size() == 1);
        CHECK(fixture.begins.front().a == paddle);
        CHECK(fixture.begins.front().b == box);
        CHECK(near(fixture.begins.front().normal, math::world::right<float>, 1.0e-3f));
        REQUIRE(fixture.ends.size() == 1);
        CHECK(fixture.ends.front().a == paddle);
        CHECK(fixture.ends.front().b == box);
        CHECK(fixture.registry().get<Transform>(box).location.x > 1.5f);
    }

    TEST_CASE("ends the contacts of a body whose entity is destroyed or loses its Collider")
    {
        Fixture fixture;
        const auto floor = fixture.addFloor();
        const auto destroyed = fixture.addBox({ -2.0f, 0.0f, 0.5f }, math::Float3{ 0.5f }, RigidBody{});
        const auto stripped = fixture.addBox({ 2.0f, 0.0f, 0.5f }, math::Float3{ 0.5f }, RigidBody{});
        fixture.world.begin();
        fixture.run(60);
        REQUIRE(fixture.begins.size() == 2);
        REQUIRE(fixture.ends.empty());

        fixture.registry().destroy(destroyed);
        fixture.registry().remove<Collider>(stripped);
        fixture.run(2);

        REQUIRE(fixture.ends.size() == 2);
        CHECK(fixture.ends[0].a == floor);
        CHECK(fixture.ends[0].b == destroyed);
        CHECK(fixture.ends[1].a == floor);
        CHECK(fixture.ends[1].b == stripped);
        CHECK(fixture.begins.size() == 2);
    }

    TEST_CASE("sends no events for bodies that never touch")
    {
        Fixture fixture;
        fixture.addFloor();
        fixture.addBox({ 0.0f, 0.0f, 5.0f }, math::Float3{ 0.5f }, RigidBody{ .gravityFactor = 0.0f });
        fixture.world.begin();
        fixture.run(60);

        CHECK(fixture.begins.empty());
        CHECK(fixture.ends.empty());
    }

    TEST_CASE("reaches a script in the update after the step where two bodies touch")
    {
        reflect::TypeRegistry types;
        REQUIRE(scripting::registerScript<ContactCounter>(types, "ContactCounter"));
        world::World world;
        world.addSystem<scripting::ScriptSystem>(); // The engine's order: scripts, then physics
        world.addSystem<physics::PhysicsSystem>();
        world.addSystem<world::TransformSystem>();

        auto& registry = world.registry();
        const auto floor = registry.create("Floor", { 0.0f, 0.0f, -0.5f });
        registry.add<Collider>(floor, Collider{ .shape = BoxShape{ .halfExtents = { 10.0f, 10.0f, 0.5f } } });
        const auto box = registry.create("Box", { 0.0f, 0.0f, 0.5f }); // Resting on the floor from the start
        registry.add<Collider>(box, Collider{});
        registry.add<RigidBody>(box, RigidBody{});
        auto script = scripting::attachScript(registry, types, box, "ContactCounter");
        REQUIRE(script);
        const auto& counter = *static_cast<ContactCounter*>(*script);

        world.begin();
        for (int frame = 0; frame < 3; ++frame)
            world.update(STEP);

        // The first update's step finds the contact; the script reads it during the second.
        CHECK(counter.counts == std::vector<std::size_t>{ 0, 1, 0 });
    }
}
