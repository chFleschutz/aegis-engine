#include <doctest/doctest.h>

import std;
import aegis.math;
import aegis.reflect;
import aegis.render_bridge;
import aegis.serialize;
import aegis.world;
import aegis.world.registry;
import aegis.world.render_components;
import aegis.world.types;

namespace
{
using aegis::Worlds;
using aegis::world::Registry;
using aegis::world::UpdateMode;
using aegis::world::World;

// What happened to one world's systems.
struct Counts
{
    int setUp{ 0 };
    int begun{ 0 };         // Mover::onBegin, in either mode
    int gameplayBegun{ 0 }; // Gameplay::onBegin, in Play only
    int reset{ 0 };
};

// By the world's address, which stays valid in the map after the world is gone.
using CountsByWorld = std::map<const World*, Counts>;

// Moves every entity along x by the update's delta, in Edit too.
class Mover final : public aegis::world::System
{
public:
    explicit Mover(Counts& counts) :
        m_counts{ counts }
    {
    }

    [[nodiscard]] auto runsInEdit() const -> bool override { return true; }
    void onBegin(Registry&) override { ++m_counts.begun; }
    void onUpdate(Registry& registry, float deltaSeconds) override
    {
        for (auto&& [entity, transform] : registry.view<aegis::Transform>().each())
            transform.location.x += deltaSeconds;
    }
    void onReset(Registry&) override { ++m_counts.reset; }

private:
    Counts& m_counts;
};

// A system that runs in Play only, like scripts.
class Gameplay final : public aegis::world::System
{
public:
    explicit Gameplay(Counts& counts) :
        m_counts{ counts }
    {
    }

    void onBegin(Registry&) override { ++m_counts.gameplayBegun; }

private:
    Counts& m_counts;
};

auto setup(CountsByWorld& counts) -> Worlds::Setup
{
    return [&counts](World& world) {
        auto& count = counts[&world];
        ++count.setUp;
        world.addSystem<Mover>(count);
        world.addSystem<Gameplay>(count);
    };
}

auto locationX(World& world, aegis::StableId id) -> float
{
    auto& registry = world.registry();
    return registry.get<aegis::Transform>(registry.find(id)).location.x;
}
}

TEST_SUITE("render_bridge::Worlds")
{
    TEST_CASE("each world is set up when it is created and again when it is reset")
    {
        CountsByWorld counts;
        Worlds worlds{ setup(counts) };
        auto& first = worlds.create();
        auto& second = worlds.create();
        CHECK(&first != &second);
        CHECK(worlds.all() == std::vector<World*>{ &first, &second });
        CHECK(counts[&first].setUp == 1);
        CHECK(counts[&second].setUp == 1);

        worlds.reset(first);
        CHECK(counts[&first].reset == 1);
        CHECK(counts[&first].setUp == 2);
        CHECK(counts[&second].setUp == 1);
    }

    TEST_CASE("two worlds from one scene: updating one leaves the other unchanged")
    {
        aegis::reflect::TypeRegistry types;
        REQUIRE(aegis::world::registerComponents(types).has_value());
        Registry source;
        const auto box = source.create("Box", glm::vec3{ 1.0f, 2.0f, 3.0f });
        const auto id = source.get<aegis::StableId>(box);
        const auto scene = aegis::serialize::saveScene(source, types, 0x00000000000000aa);
        REQUIRE(scene.has_value());

        CountsByWorld counts;
        Worlds worlds{ setup(counts) };
        auto& played = worlds.create();
        auto& kept = worlds.create();
        REQUIRE(aegis::serialize::instantiate(played.registry(), types, *scene).has_value());
        REQUIRE(aegis::serialize::instantiate(kept.registry(), types, *scene).has_value());

        worlds.update(played, 0.5f, UpdateMode::Play);
        worlds.update(played, 0.25f, UpdateMode::Play);
        CHECK(locationX(played, id) == 1.75f);
        CHECK(locationX(kept, id) == 1.0f);
        CHECK(counts[&played].begun == 1);
        CHECK(counts[&kept].begun == 0);
    }

    TEST_CASE("a world begins with its first update, in that update's mode, and again after a reset")
    {
        CountsByWorld counts;
        Worlds worlds{ setup(counts) };
        auto& played = worlds.create();
        auto& edited = worlds.create();
        CHECK(counts[&played].begun == 0);

        worlds.update(played, 0.1f, UpdateMode::Play);
        worlds.update(played, 0.1f, UpdateMode::Play);
        worlds.update(edited, 0.1f, UpdateMode::Edit);
        CHECK(counts[&played].begun == 1);
        CHECK(counts[&played].gameplayBegun == 1);
        CHECK(counts[&edited].begun == 1);
        CHECK(counts[&edited].gameplayBegun == 0);

        worlds.reset(played);
        CHECK(counts[&played].begun == 1);
        worlds.update(played, 0.1f, UpdateMode::Play);
        CHECK(counts[&played].begun == 2);
    }

    TEST_CASE("each world has instances of its own, followed across a reset")
    {
        CountsByWorld counts;
        Worlds worlds{ setup(counts) };
        auto& first = worlds.create();
        auto& second = worlds.create();
        CHECK(&worlds.instances(first) != &worlds.instances(second));

        // Meshes need a device, so the renderer draws nothing: its RenderInstance shows the bridge follows it.
        auto& registry = first.registry();
        const auto drawn = registry.create();
        registry.add<aegis::MeshRenderer>(drawn);
        CHECK(registry.has<aegis::RenderInstance>(drawn));

        worlds.reset(first);
        const auto again = registry.create();
        registry.add<aegis::MeshRenderer>(again);
        CHECK(registry.has<aegis::RenderInstance>(again));
        CHECK(second.registry().view<aegis::RenderInstance>().empty());
    }

    TEST_CASE("destroying a world ends its systems and leaves the others")
    {
        CountsByWorld counts;
        Worlds worlds{ setup(counts) };
        auto& doomed = worlds.create();
        auto& kept = worlds.create();
        const auto* address = &doomed;
        const auto entity = kept.registry().create();
        kept.registry().add<aegis::MeshRenderer>(entity);

        worlds.destroy(doomed);
        CHECK(counts[address].reset == 1);
        CHECK(counts[&kept].reset == 0);
        CHECK(worlds.all() == std::vector<World*>{ &kept });
        CHECK(worlds.contains(kept));
        CHECK(kept.registry().has<aegis::RenderInstance>(entity));

        worlds.update(kept, 0.1f, UpdateMode::Play);
        CHECK(counts[&kept].begun == 1);
    }
}
