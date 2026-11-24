#include <doctest/doctest.h>

import std;
import aegis.math;
import aegis.physics;
import aegis.reflect;
import aegis.serialize;
import aegis.world;

namespace
{
using namespace aegis;
using physics::BoxShape;
using physics::CapsuleShape;
using physics::Collider;
using physics::MotionType;
using physics::RigidBody;
using physics::SphereShape;

auto registerTypes(reflect::TypeRegistry& types) -> void
{
    REQUIRE(world::registerComponents(types).has_value());
    REQUIRE(physics::registerComponents(types).has_value());
}

auto saveText(world::Registry& registry, const reflect::TypeRegistry& types) -> std::string
{
    auto scene = serialize::saveScene(registry, types, 0x5e3a91c4d07b2f68);
    REQUIRE(scene.has_value());
    return serialize::writeScene(*scene);
}

auto load(const std::string& text, const reflect::TypeRegistry& types, world::Registry& registry) -> void
{
    auto scene = serialize::parseScene(text, types);
    REQUIRE(scene.has_value());
    REQUIRE(serialize::instantiate(registry, types, *scene).has_value());
}

auto named(world::Registry& registry, std::string_view name) -> world::Entity
{
    for (const auto entity : registry.view<Name>())
    {
        if (registry.get<Name>(world::Entity{ entity }).name == name)
            return world::Entity{ entity };
    }
    return {};
}
}

TEST_SUITE("physics::components")
{
    TEST_CASE("loads a saved RigidBody and Collider back the same")
    {
        reflect::TypeRegistry types;
        registerTypes(types);
        world::Registry original;
        const auto crate = original.create("Crate", { 1.0f, 2.0f, 3.0f });
        original.add<RigidBody>(crate, RigidBody{
                                           .motionType = MotionType::Kinematic,
                                           .mass = 12.5f,
                                           .friction = 0.8f,
                                           .restitution = 0.25f,
                                           .linearDamping = 0.1f,
                                           .angularDamping = 0.2f,
                                           .gravityFactor = 0.5f,
                                       });
        original.add<Collider>(crate, Collider{ .shape = BoxShape{ .halfExtents = { 0.5f, 1.0f, 1.5f } }, .offset = { 0.0f, 0.0f, 0.25f } });
        const auto pill = original.create("Pill");
        original.add<Collider>(pill, Collider{ .shape = CapsuleShape{ .radius = 0.3f, .halfHeight = 0.9f } });
        const auto ball = original.create("Ball");
        original.add<Collider>(ball, Collider{ .shape = SphereShape{ .radius = 2.0f } });
        const std::string text = saveText(original, types);

        world::Registry loaded;
        load(text, types, loaded);
        CHECK(saveText(loaded, types) == text);

        const auto& body = loaded.get<RigidBody>(named(loaded, "Crate"));
        CHECK(body.motionType == MotionType::Kinematic);
        CHECK(body.mass == 12.5f);
        CHECK(body.friction == 0.8f);
        CHECK(body.restitution == 0.25f);
        CHECK(body.linearDamping == 0.1f);
        CHECK(body.angularDamping == 0.2f);
        CHECK(body.gravityFactor == 0.5f);

        const auto& box = loaded.get<Collider>(named(loaded, "Crate"));
        REQUIRE(std::holds_alternative<BoxShape>(box.shape));
        CHECK(std::get<BoxShape>(box.shape).halfExtents == math::Float3{ 0.5f, 1.0f, 1.5f });
        CHECK(box.offset == math::Float3{ 0.0f, 0.0f, 0.25f });

        const auto& capsule = loaded.get<Collider>(named(loaded, "Pill"));
        REQUIRE(std::holds_alternative<CapsuleShape>(capsule.shape));
        CHECK(std::get<CapsuleShape>(capsule.shape).radius == 0.3f);
        CHECK(std::get<CapsuleShape>(capsule.shape).halfHeight == 0.9f);

        const auto& sphere = loaded.get<Collider>(named(loaded, "Ball"));
        REQUIRE(std::holds_alternative<SphereShape>(sphere.shape));
        CHECK(std::get<SphereShape>(sphere.shape).radius == 2.0f);
    }

    TEST_CASE("switches a Collider's shape through its shape field")
    {
        reflect::TypeRegistry types;
        registerTypes(types);
        const auto* info = types.find<Collider>();
        REQUIRE(info != nullptr);

        Collider collider;
        CHECK(std::get<std::int64_t>(*info->get(&collider, "shape")) == static_cast<std::int64_t>(physics::ShapeType::Box));
        REQUIRE(info->setText(&collider, "shape", "Capsule").has_value());
        REQUIRE(std::holds_alternative<CapsuleShape>(collider.shape));
        REQUIRE(info->set(&collider, "radius", 0.25f).has_value());
        REQUIRE(info->set(&collider, "halfHeight", 0.75f).has_value());
        CHECK(std::get<CapsuleShape>(collider.shape).radius == 0.25f);
        CHECK(std::get<CapsuleShape>(collider.shape).halfHeight == 0.75f);

        // Setting the shape it has keeps its sizes.
        REQUIRE(info->setText(&collider, "shape", "Capsule").has_value());
        CHECK(std::get<CapsuleShape>(collider.shape).radius == 0.25f);
    }

    TEST_CASE("reads a field the shape lacks as that shape's default and ignores writes to it")
    {
        reflect::TypeRegistry types;
        registerTypes(types);
        const auto* info = types.find<Collider>();
        Collider collider{ .shape = BoxShape{ .halfExtents = math::Float3{ 2.0f } } };

        CHECK(std::get<float>(*info->get(&collider, "radius")) == SphereShape{}.radius);
        CHECK(std::get<float>(*info->get(&collider, "halfHeight")) == CapsuleShape{}.halfHeight);
        REQUIRE(info->set(&collider, "radius", 9.0f).has_value());
        REQUIRE(std::holds_alternative<BoxShape>(collider.shape));
        CHECK(std::get<BoxShape>(collider.shape).halfExtents == math::Float3{ 2.0f });
    }
}
