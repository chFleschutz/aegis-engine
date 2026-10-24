#include <doctest/doctest.h>

import std;
import aegis.editor.core;
import aegis.math;
import aegis.reflect;
import aegis.scripting.script;
import aegis.world;
import aegis.world.types;

namespace
{
using aegis::Name;
using aegis::StableId;
using aegis::Transform;
using aegis::editor::copyWorld;
using aegis::editor::PlaySession;
using aegis::editor::sceneText;
using aegis::reflect::TypeRegistry;
using aegis::world::World;

struct Health
{
    float value{ 100.0f };
};

struct Patrol : aegis::scripting::Script
{
    float speed{ 1.0f };
};

// A device-less edit world with the built-in and the game's types: a parent with a child, a component and a script.
struct Fixture
{
    Fixture()
    {
        REQUIRE(aegis::world::registerComponents(types).has_value());
        auto health = aegis::world::registerComponent<Health>(types, "Health");
        REQUIRE(health.has_value());
        health->field("value", &Health::value);
        auto patrol = aegis::scripting::registerScript<Patrol>(types, "Patrol");
        REQUIRE(patrol.has_value());
        patrol->field("speed", &Patrol::speed);

        auto& registry = edit.registry();
        const auto parent = registry.create("Guard", glm::vec3{ 1.0f, 2.0f, 3.0f });
        registry.add<Health>(parent).value = 42.0f;
        auto script = aegis::scripting::attachScript(registry, types, parent, "Patrol");
        REQUIRE(script.has_value());
        static_cast<Patrol*>(*script)->speed = 3.0f;
        const auto child = registry.create("Lamp");
        REQUIRE(registry.setParent(child, parent).has_value());
        guard = registry.get<StableId>(parent);
    }

    auto text(World& world) -> std::string
    {
        auto written = sceneText(world.registry(), types, 7);
        REQUIRE(written.has_value());
        return *written;
    }

    TypeRegistry types;
    World edit;
    StableId guard;
};
}

TEST_SUITE("editor::copyWorld")
{
    TEST_CASE("copies every entity with its StableId, components, scripts and hierarchy")
    {
        Fixture f;
        World play;
        REQUIRE(copyWorld(f.edit.registry(), play.registry(), f.types).has_value());
        CHECK(f.text(play) == f.text(f.edit));
    }

    TEST_CASE("a play session leaves the edit world byte-identical")
    {
        Fixture f;
        const auto before = f.text(f.edit);
        {
            World play;
            REQUIRE(copyWorld(f.edit.registry(), play.registry(), f.types).has_value());
            auto& registry = play.registry();
            for (auto&& [entity, transform] : registry.view<Transform>().each())
                transform.location += glm::vec3{ 5.0f };
            for (auto&& [entity, health] : registry.view<Health>().each())
                health.value = 0.0f;
            registry.create("Spawned");
            registry.destroy(registry.find(f.guard));
        }
        CHECK(f.text(f.edit) == before);
    }

    TEST_CASE("leaves the target unchanged when an ID is taken")
    {
        Fixture f;
        World play;
        REQUIRE(copyWorld(f.edit.registry(), play.registry(), f.types).has_value());
        const auto copied = f.text(play);
        CHECK_FALSE(copyWorld(f.edit.registry(), play.registry(), f.types).has_value());
        CHECK(f.text(play) == copied);
    }
}

TEST_SUITE("editor::PlaySession")
{
    TEST_CASE("is stopped at first and updates nothing")
    {
        PlaySession session;
        CHECK(session.state() == PlaySession::State::Stopped);
        CHECK_FALSE(session.active());
        CHECK_FALSE(session.shouldUpdate());
    }

    TEST_CASE("updates every frame while playing")
    {
        PlaySession session;
        session.play();
        CHECK(session.active());
        CHECK(session.shouldUpdate());
        CHECK(session.shouldUpdate());
    }

    TEST_CASE("a step runs one frame of a paused session")
    {
        PlaySession session;
        session.play();
        session.pause();
        CHECK(session.state() == PlaySession::State::Paused);
        CHECK_FALSE(session.shouldUpdate());
        session.step();
        CHECK(session.shouldUpdate());
        CHECK_FALSE(session.shouldUpdate());
    }

    TEST_CASE("pause and step do nothing while stopped")
    {
        PlaySession session;
        session.pause();
        session.step();
        CHECK(session.state() == PlaySession::State::Stopped);
        CHECK_FALSE(session.shouldUpdate());
    }

    TEST_CASE("play resumes a paused session and stop ends it")
    {
        PlaySession session;
        session.play();
        session.pause();
        session.play();
        CHECK(session.state() == PlaySession::State::Playing);
        session.stop();
        CHECK_FALSE(session.active());
        CHECK_FALSE(session.shouldUpdate());
    }
}
