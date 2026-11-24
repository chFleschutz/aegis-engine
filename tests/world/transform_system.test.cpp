#include <doctest/doctest.h>

import std;
import aegis.math;
import aegis.world;

namespace math = aegis::math;

namespace
{
using aegis::DynamicTag;
using aegis::GlobalTransform;
using aegis::Transform;
using aegis::world::Registry;
using aegis::world::TransformSystem;

using doctest::Approx;

// Approx has a default scale of 1, so components near zero get an absolute margin too.
auto near(const math::Float3& a, const math::Float3& b) -> bool
{
    return a.x == Approx(b.x).epsilon(1e-4) && a.y == Approx(b.y).epsilon(1e-4) && a.z == Approx(b.z).epsilon(1e-4);
}

auto quarterTurnZ() -> math::Quat
{
    return math::Quat::fromAxisAngle(math::Float3{ 0.0f, 0.0f, 1.0f }, std::numbers::pi_v<float> / 2.0f);
}
}

TEST_SUITE("world::TransformSystem")
{
    TEST_CASE("applies the parent's rotation and scale to a child's location")
    {
        Registry registry;
        const auto parent = registry.create("parent", { 0.0f, 0.0f, 0.0f }, quarterTurnZ(), math::Float3{ 2.0f });
        const auto child = registry.create("child", { 1.0f, 0.0f, 0.0f });
        REQUIRE(registry.setParent(child, parent));

        TransformSystem system;
        system.onBegin(registry);

        const auto& global = registry.get<GlobalTransform>(child);
        CHECK(near(global.location(), { 0.0f, 2.0f, 0.0f }));
        CHECK(near(global.scale(), math::Float3{ 2.0f }));
        CHECK(near(global.rotation() * math::Float3{ 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }));
    }

    TEST_CASE("keeps the shear of a rotated child under a non-uniformly scaled parent")
    {
        Registry registry;
        const auto parent = registry.create("parent", { 1.0f, 2.0f, 3.0f }, math::Quat::identity(), math::Float3{ 1.0f, 3.0f, 1.0f });
        const auto child = registry.create("child", { 0.5f, 0.0f, 0.0f }, math::Quat::fromAxisAngle(math::Float3{ 0.0f, 0.0f, 1.0f }, 0.6f));
        REQUIRE(registry.setParent(child, parent));
        registry.add<DynamicTag>(child);

        TransformSystem system;
        system.onBegin(registry);
        const auto expected = registry.get<Transform>(parent).matrix() * registry.get<Transform>(child).matrix();
        CHECK(registry.get<GlobalTransform>(child).matrix == expected);

        // The child's axes are no longer perpendicular: TRS could not hold this
        CHECK(math::dot(registry.get<GlobalTransform>(child).right(), registry.get<GlobalTransform>(child).forward()) != Approx(0.0f));

        system.onUpdate(registry, 0.0f);
        CHECK(registry.get<GlobalTransform>(child).matrix == expected);
    }

    TEST_CASE("adds the parent's location and composes a chain of three")
    {
        Registry registry;
        const auto root = registry.create("root", { 10.0f, 0.0f, 0.0f }, quarterTurnZ());
        const auto middle = registry.create("middle", { 1.0f, 0.0f, 0.0f });
        const auto leaf = registry.create("leaf", { 1.0f, 0.0f, 0.0f });
        REQUIRE(registry.setParent(middle, root));
        REQUIRE(registry.setParent(leaf, middle));

        TransformSystem system;
        system.onBegin(registry);

        // middle sits at (10,1,0); leaf is one unit further along the root's rotated x axis
        CHECK(near(registry.get<GlobalTransform>(middle).location(), { 10.0f, 1.0f, 0.0f }));
        CHECK(near(registry.get<GlobalTransform>(leaf).location(), { 10.0f, 2.0f, 0.0f }));
    }

    TEST_CASE("updates only entities with DynamicTag after begin")
    {
        Registry registry;
        const auto parent = registry.create("parent", { 0.0f, 0.0f, 0.0f }, quarterTurnZ());
        const auto dynamicChild = registry.create("dynamic", { 1.0f, 0.0f, 0.0f });
        const auto staticChild = registry.create("static", { 1.0f, 0.0f, 0.0f });
        REQUIRE(registry.setParent(dynamicChild, parent));
        REQUIRE(registry.setParent(staticChild, parent));
        registry.add<DynamicTag>(parent);
        registry.add<DynamicTag>(dynamicChild);

        TransformSystem system;
        system.onBegin(registry);
        CHECK(near(registry.get<GlobalTransform>(dynamicChild).location(), { 0.0f, 1.0f, 0.0f }));

        registry.get<Transform>(parent).location = { 5.0f, 0.0f, 0.0f };
        registry.get<Transform>(dynamicChild).location = { 2.0f, 0.0f, 0.0f };
        registry.get<Transform>(staticChild).location = { 2.0f, 0.0f, 0.0f };
        system.onUpdate(registry, 0.016f);
        system.onUpdate(registry, 0.016f);

        CHECK(near(registry.get<GlobalTransform>(parent).location(), { 5.0f, 0.0f, 0.0f }));
        CHECK(near(registry.get<GlobalTransform>(dynamicChild).location(), { 5.0f, 2.0f, 0.0f }));
        CHECK(near(registry.get<GlobalTransform>(staticChild).location(), { 0.0f, 1.0f, 0.0f }));
    }
}

TEST_SUITE("world::TransformSystem static dirty")
{
    TEST_CASE("recomputes a marked static entity and its descendants, then clears the mark")
    {
        Registry registry;
        const auto parent = registry.create("parent");
        const auto child = registry.create("child", { 1.0f, 0.0f, 0.0f });
        REQUIRE(registry.setParent(child, parent));

        TransformSystem system;
        system.onBegin(registry);
        CHECK(near(registry.get<GlobalTransform>(child).location(), { 1.0f, 0.0f, 0.0f }));

        registry.get<Transform>(parent).location = { 0.0f, 5.0f, 0.0f };
        system.onUpdate(registry, 0.0f);
        CHECK(near(registry.get<GlobalTransform>(child).location(), { 1.0f, 0.0f, 0.0f })); // Static: frozen

        registry.markStaticDirty(parent);
        const auto marked = registry.staticVersion();
        system.onUpdate(registry, 0.0f);

        CHECK(near(registry.get<GlobalTransform>(parent).location(), { 0.0f, 5.0f, 0.0f }));
        CHECK(near(registry.get<GlobalTransform>(child).location(), { 1.0f, 5.0f, 0.0f }));
        CHECK(!registry.has<aegis::StaticDirtyTag>(parent));
        CHECK(registry.staticVersion() != marked); // Whoever rebuilt after the mark rebuilds again
    }

    TEST_CASE("leaves the static version alone when nothing is marked")
    {
        Registry registry;
        registry.create();
        TransformSystem system;
        const auto before = registry.staticVersion();
        system.onUpdate(registry, 0.0f);
        CHECK(registry.staticVersion() == before);
    }
}

TEST_SUITE("world::detail::toLocal")
{
    TEST_CASE("is the inverse of compose")
    {
        const GlobalTransform parent{ math::transformMatrix(math::Float3{ 1.0f, -2.0f, 3.0f }, quarterTurnZ(), math::Float3{ 2.0f }) };
        const Transform local{ { 0.5f, 1.5f, -1.0f }, math::Quat::fromAxisAngle(math::normalize(math::Float3{ 1.0f, 1.0f, 0.0f }), 0.7f),
            math::Float3{ 0.5f, 0.5f, 0.5f } };

        const auto global = aegis::world::detail::compose(parent, local);
        const auto back = aegis::world::detail::toLocal(parent, global.matrix);

        CHECK(near(back.location, local.location));
        CHECK(near(back.scale, local.scale));
        CHECK(near(back.rotation * math::Float3{ 0.0f, 0.0f, 1.0f }, local.rotation * math::Float3{ 0.0f, 0.0f, 1.0f }));
        CHECK(near(back.rotation * math::Float3{ 1.0f, 0.0f, 0.0f }, local.rotation * math::Float3{ 1.0f, 0.0f, 0.0f }));
    }

    TEST_CASE("returns the world matrix unchanged below an identity parent")
    {
        const Transform world{ { 3.0f, 4.0f, 5.0f }, quarterTurnZ(), { 1.0f, 2.0f, 3.0f } };

        const auto local = aegis::world::detail::toLocal(GlobalTransform{}, world.matrix());

        CHECK(near(local.location, world.location));
        CHECK(near(local.scale, world.scale));
    }
}

TEST_SUITE("world::TransformSystem hierarchy order")
{
    TEST_CASE("a chain of three dynamic entities is right after one update, whatever order they were tagged in")
    {
        for (const bool rootFirst : { true, false })
        {
            CAPTURE(rootFirst);
            aegis::world::World scene;
            scene.addSystem<TransformSystem>();
            auto& registry = scene.registry();
            const auto root = registry.create("root");
            const auto middle = registry.create("middle", { 1.0f, 0.0f, 0.0f });
            const auto leaf = registry.create("leaf", { 1.0f, 0.0f, 0.0f });
            REQUIRE(registry.setParent(middle, root));
            REQUIRE(registry.setParent(leaf, middle));
            for (const auto entity : rootFirst ? std::array{ root, middle, leaf } : std::array{ leaf, middle, root })
                registry.add<DynamicTag>(entity);
            scene.begin();

            registry.get<Transform>(root).location = { 10.0f, 0.0f, 0.0f };
            registry.get<Transform>(root).rotation = quarterTurnZ();
            registry.get<Transform>(middle).location = { 2.0f, 0.0f, 0.0f };
            registry.get<Transform>(leaf).location = { 3.0f, 0.0f, 0.0f };
            scene.update(0.0f);

            CHECK(near(registry.get<GlobalTransform>(root).location(), { 10.0f, 0.0f, 0.0f }));
            CHECK(near(registry.get<GlobalTransform>(middle).location(), { 10.0f, 2.0f, 0.0f }));
            CHECK(near(registry.get<GlobalTransform>(leaf).location(), { 10.0f, 5.0f, 0.0f }));
        }
    }
}

TEST_SUITE("world::TransformSystem DynamicTag at run time")
{
    TEST_CASE("an entity tagged after begin follows its Transform from the next update")
    {
        aegis::world::World scene;
        scene.addSystem<TransformSystem>();
        auto& registry = scene.registry();
        const auto entity = registry.create("entity");
        scene.begin();

        registry.get<Transform>(entity).location = { 4.0f, 0.0f, 0.0f };
        scene.update(0.0f);
        CHECK(near(registry.get<GlobalTransform>(entity).location(), { 0.0f, 0.0f, 0.0f })); // Static: frozen

        registry.add<DynamicTag>(entity);
        scene.update(0.0f);
        CHECK(near(registry.get<GlobalTransform>(entity).location(), { 4.0f, 0.0f, 0.0f }));
    }

    TEST_CASE("an entity that loses its tag stays where its Transform puts it, then freezes")
    {
        aegis::world::World scene;
        scene.addSystem<TransformSystem>();
        auto& registry = scene.registry();
        const auto entity = registry.create("entity");
        const auto child = registry.create("child", { 1.0f, 0.0f, 0.0f });
        REQUIRE(registry.setParent(child, entity));
        registry.add<DynamicTag>(entity);
        scene.begin();
        scene.update(0.0f);

        // Moved and made static in the same frame: the move still counts, for the static child too
        registry.get<Transform>(entity).location = { 4.0f, 0.0f, 0.0f };
        registry.remove<DynamicTag>(entity);
        const auto before = registry.staticVersion();
        scene.update(0.0f);
        CHECK(near(registry.get<GlobalTransform>(entity).location(), { 4.0f, 0.0f, 0.0f }));
        CHECK(near(registry.get<GlobalTransform>(child).location(), { 5.0f, 0.0f, 0.0f }));
        CHECK(registry.staticVersion() != before); // The static set it joined is rebuilt

        registry.get<Transform>(entity).location = { 8.0f, 0.0f, 0.0f };
        scene.update(0.0f);
        CHECK(near(registry.get<GlobalTransform>(entity).location(), { 4.0f, 0.0f, 0.0f }));
    }

    TEST_CASE("a destroyed dynamic entity leaves nothing to recompute")
    {
        aegis::world::World scene;
        scene.addSystem<TransformSystem>();
        auto& registry = scene.registry();
        const auto entity = registry.create("entity");
        registry.add<DynamicTag>(entity);
        scene.begin();
        registry.destroy(entity);
        const auto before = registry.staticVersion();
        scene.update(0.0f);
        CHECK(registry.staticVersion() == before);
    }
}
