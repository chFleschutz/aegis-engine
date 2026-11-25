#include <doctest/doctest.h>

import std;
import aegis.math;
import aegis.physics;
import aegis.world;

namespace
{
using namespace aegis;
using physics::BoxShape;
using physics::CharacterController;
using physics::Collider;
using physics::ContactBegin;
using physics::ContactEnd;
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
    auto system() -> physics::PhysicsSystem& { return *registry().context<physics::PhysicsSystem>(); }

    auto addBox(const math::Float3& location, const math::Float3& halfExtents, std::optional<RigidBody> body = std::nullopt) -> world::Entity
    {
        const auto entity = registry().create("Box", location);
        registry().add<Collider>(entity, Collider{ .shape = BoxShape{ .halfExtents = halfExtents } });
        if (body)
            registry().add<RigidBody>(entity, *body);
        return entity;
    }

    // A static floor whose top is at z = 0.
    auto addFloor() -> world::Entity { return addBox({ 0.0f, 0.0f, -0.5f }, { 20.0f, 20.0f, 0.5f }); }

    // A character standing with its feet at 'feet'.
    auto addCharacter(const math::Float3& feet) -> world::Entity
    {
        const auto entity = registry().create("Character", feet);
        registry().add<CharacterController>(entity);
        return entity;
    }

    auto controller(world::Entity entity) -> CharacterController& { return registry().get<CharacterController>(entity); }
    auto location(world::Entity entity) -> math::Float3 { return registry().get<Transform>(entity).location; }

    // Updates the world 'frames' times by one fixed step each and keeps the events it can read.
    void run(int frames)
    {
        for (int frame = 0; frame < frames; ++frame)
        {
            world.update(STEP);
            std::ranges::copy(registry().events().read<ContactBegin>(), std::back_inserter(begins));
            std::ranges::copy(registry().events().read<ContactEnd>(), std::back_inserter(ends));
        }
    }

    auto touches(world::Entity x, world::Entity y) const -> std::size_t
    {
        return static_cast<std::size_t>(std::ranges::count_if(begins, [x, y](const ContactBegin& c) { return (c.a == x && c.b == y) || (c.a == y && c.b == x); }));
    }
};
}

TEST_SUITE("physics::CharacterController")
{
    TEST_CASE("falls onto the floor and stands on it, grounded")
    {
        Fixture fixture;
        const auto floor = fixture.addFloor();
        const auto character = fixture.addCharacter({ 1.0f, 2.0f, 2.0f });
        fixture.world.begin();
        CHECK(fixture.system().hasCharacter(character));
        CHECK(fixture.registry().has<DynamicTag>(character));
        CHECK_FALSE(fixture.controller(character).grounded);

        fixture.run(180);
        CHECK(fixture.controller(character).grounded);
        const auto feet = fixture.location(character);
        CHECK(std::abs(feet.z) < 0.05f);
        CHECK(feet.x == doctest::Approx(1.0f).epsilon(1.0e-3));
        CHECK(feet.y == doctest::Approx(2.0f).epsilon(1.0e-3));

        // Standing on the floor is one contact, however long it stands
        CHECK(fixture.touches(character, floor) == 1);
        CHECK(fixture.ends.empty());
    }

    TEST_CASE("walks by its desired velocity and keeps its Transform's rotation")
    {
        Fixture fixture;
        fixture.addFloor();
        const auto character = fixture.addCharacter(math::Float3{ 0.0f });
        const auto turned = math::Quat::fromAxisAngle(math::world::up<float>, 1.0f);
        fixture.registry().get<Transform>(character).rotation = turned;
        fixture.world.begin();
        fixture.run(30);

        fixture.controller(character).desiredVelocity = { 0.0f, 2.0f, 0.0f };
        fixture.run(60);
        const auto feet = fixture.location(character);
        CHECK(feet.y == doctest::Approx(2.0f).epsilon(0.05));
        CHECK(std::abs(feet.x) < 1.0e-3f);
        CHECK(std::abs(feet.z) < 0.05f);
        CHECK(fixture.registry().get<Transform>(character).rotation == turned);
    }

    TEST_CASE("jumps by the vertical part of its desired velocity while grounded")
    {
        Fixture fixture;
        fixture.addFloor();
        const auto character = fixture.addCharacter(math::Float3{ 0.0f });
        fixture.world.begin();
        fixture.run(30);
        REQUIRE(fixture.controller(character).grounded);

        fixture.controller(character).desiredVelocity = { 0.0f, 0.0f, 5.0f };
        fixture.run(1);
        fixture.controller(character).desiredVelocity = math::Float3{ 0.0f };
        fixture.run(20);
        // 5 m/s up against 9.81 m/s² down: about 1.2 m high after a third of a second
        CHECK(fixture.location(character).z > 0.8f);
        CHECK_FALSE(fixture.controller(character).grounded);

        fixture.run(120);
        CHECK(fixture.controller(character).grounded);
        CHECK(std::abs(fixture.location(character).z) < 0.05f);
    }

    TEST_CASE("pushes a dynamic box it walks into")
    {
        Fixture fixture;
        fixture.addFloor();
        const auto character = fixture.addCharacter(math::Float3{ 0.0f });
        const auto box = fixture.addBox({ 2.0f, 0.0f, 0.5f }, math::Float3{ 0.5f }, RigidBody{ .mass = 10.0f });
        fixture.world.begin();
        fixture.run(30);

        fixture.controller(character).desiredVelocity = { 2.0f, 0.0f, 0.0f };
        fixture.run(120);

        CHECK(fixture.location(box).x > 3.0f);
        CHECK(fixture.location(character).x > 2.0f);
        CHECK(fixture.touches(character, box) >= 1);
    }

    TEST_CASE("climbs a step lower than its step height and stops at a higher one")
    {
        for (const float step : { 0.2f, 0.5f })
        {
            CAPTURE(step);
            Fixture fixture;
            fixture.addFloor();
            fixture.addBox({ 3.0f, 0.0f, 0.5f * step }, { 2.0f, 5.0f, 0.5f * step }); // Its near face at x = 1
            const auto character = fixture.addCharacter(math::Float3{ 0.0f });
            REQUIRE(fixture.controller(character).stepHeight == doctest::Approx(0.3f));
            fixture.world.begin();
            fixture.run(30);

            fixture.controller(character).desiredVelocity = { 2.0f, 0.0f, 0.0f };
            fixture.run(90);

            const auto feet = fixture.location(character);
            if (step < fixture.controller(character).stepHeight)
            {
                CHECK(feet.x > 2.0f);
                CHECK(feet.z == doctest::Approx(step).epsilon(0.05));
                CHECK(fixture.controller(character).grounded);
            }
            else
            {
                CHECK(feet.x < 1.0f);
                CHECK(std::abs(feet.z) < 0.05f);
            }
        }
    }

    TEST_CASE("is found by ray casts through its inner body and lands boxes on its head")
    {
        Fixture fixture;
        const auto floor = fixture.addFloor();
        const auto character = fixture.addCharacter(math::Float3{ 0.0f });
        const auto box = fixture.addBox({ 0.0f, 0.0f, 3.0f }, math::Float3{ 0.25f }, RigidBody{});
        fixture.world.begin();
        fixture.run(120);

        // The box rests on top of the 1.8 m capsule instead of falling through it
        CHECK(fixture.location(box).z == doctest::Approx(1.8f + 0.25f).epsilon(0.02));
        CHECK(fixture.touches(character, box) == 1);

        const auto hit = physics::raycast(fixture.registry(), { -3.0f, 0.0f, 1.0f }, math::world::right<float>, 10.0f);
        REQUIRE(hit.has_value());
        CHECK(hit->entity == character);
        CHECK(hit->distance == doctest::Approx(3.0f - 0.3f).epsilon(0.01));
        const auto past = physics::raycast(fixture.registry(), { 0.0f, 0.0f, 1.0f }, -math::world::up<float>, 10.0f, character);
        REQUIRE(past.has_value());
        CHECK(past->entity == floor);
    }

    TEST_CASE("makes an entity with a CharacterController and a Collider a character, with no body")
    {
        Fixture fixture;
        fixture.addFloor();
        const auto character = fixture.addCharacter({ 0.0f, 0.0f, 1.0f });
        fixture.registry().add<Collider>(character);
        fixture.registry().add<RigidBody>(character);
        fixture.world.begin();

        CHECK(fixture.system().hasCharacter(character));
        CHECK_FALSE(fixture.system().hasBody(character));
        CHECK(fixture.system().bodyCount() == 1); // The floor

        // Without its CharacterController it becomes the body of its Collider
        fixture.registry().remove<CharacterController>(character);
        CHECK_FALSE(fixture.system().hasCharacter(character));
        fixture.run(1);
        CHECK(fixture.system().hasBody(character));
    }

    TEST_CASE("is no character with a height not above twice its radius")
    {
        Fixture fixture;
        const auto character = fixture.addCharacter(math::Float3{ 0.0f });
        fixture.controller(character) = CharacterController{ .radius = 0.5f, .height = 1.0f };
        fixture.world.begin();
        CHECK_FALSE(fixture.system().hasCharacter(character));
    }
}
