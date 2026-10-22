#include <doctest/doctest.h>

import std;
import aegis.reflect;
import aegis.world;
import aegis.scripting.script;
import aegis.scripting.script_system;

namespace
{
using namespace aegis;
using namespace aegis::scripting;
using aegis::reflect::TypeRegistry;
using aegis::world::Entity;
using aegis::world::World;

// What the test scripts did, in order: "<tag>.<hook>".
std::vector<std::string> g_log;

// Writes its hooks to g_log. `tag` is a registered field, so each instance can be told apart.
struct Recorder : Script
{
    std::string tag{ "rec" };

    void onCreate(ScriptContext&) override { g_log.push_back(tag + ".create"); }
    void onUpdate(ScriptContext&, float) override { g_log.push_back(tag + ".update"); }
    void onDestroy(ScriptContext& context) override
    {
        // "!" marks an entity that already lost its components
        const bool intact = context.has<Transform, Name>() && context.registry().valid(context.entity());
        g_log.push_back(tag + ".destroy" + (intact ? "" : "!"));
    }
};

// Moves its entity every update, by a registered speed
struct Mover : Script
{
    float speed{ 1.0f };
    void onUpdate(ScriptContext& context, float deltaSeconds) override
    {
        context.get<Transform>().location.x += speed * deltaSeconds;
    }
};

// Destroys its own entity in the first update
struct Suicide : Script
{
    void onUpdate(ScriptContext& context, float) override
    {
        g_log.push_back("suicide.update");
        context.registry().destroy(context.entity());
        g_log.push_back("suicide.after");
    }
    void onDestroy(ScriptContext&) override { g_log.push_back("suicide.destroy"); }
};

// Spawns an entity with a Recorder in the first update
struct Spawner : Script
{
    const TypeRegistry* types{ nullptr };
    bool spawned{ false };
    void onUpdate(ScriptContext& context, float) override
    {
        if (spawned)
            return;
        spawned = true;
        const auto child = context.registry().create("Spawned");
        CHECK(attachScript(context.registry(), *types, child, "Recorder"));
    }
};

struct Orphan : Script
{
};

struct NoDefault : Script
{
    explicit NoDefault(int) {}
};

// A script whose Script base is not at the start of the object
struct Mixin
{
    int padding[3]{ 1, 2, 3 };
    virtual ~Mixin() = default;
};

struct MultiBase : Mixin, Script
{
    void onUpdate(ScriptContext&, float) override { g_log.push_back("multi.update"); }
};

struct Fixture
{
    Fixture()
    {
        g_log.clear();
        REQUIRE(registerScript<Recorder>(types, "Recorder")->field("tag", &Recorder::tag).status());
        REQUIRE(registerScript<Mover>(types, "Mover")->field("speed", &Mover::speed).status());
        REQUIRE(registerScript<Suicide>(types, "Suicide"));
        REQUIRE(registerScript<Spawner>(types, "Spawner"));
        REQUIRE(registerScript<NoDefault>(types, "NoDefault"));
        REQUIRE(registerScript<MultiBase>(types, "MultiBase"));
        REQUIRE(types.script<Orphan>("Orphan"));
        REQUIRE(types.component<Transform>("Transform"));
        scene.addSystem<ScriptSystem>(scene.registry());
    }

    auto spawn(std::string_view script, const std::string& name = "Entity") -> Entity
    {
        const auto entity = scene.registry().create(name);
        REQUIRE(attachScript(scene.registry(), types, entity, script));
        return entity;
    }

    auto spawnRecorder(const std::string& tag) -> Entity
    {
        const auto entity = scene.registry().create(tag);
        auto script = attachScript(scene.registry(), types, entity, "Recorder");
        REQUIRE(script);
        static_cast<Recorder*>(*script)->tag = tag;
        return entity;
    }

    TypeRegistry types;
    World scene;
};
}

TEST_SUITE("scripting::attachScript")
{
    TEST_CASE("creates the script registered under the name")
    {
        Fixture f;
        const auto entity = f.scene.registry().create();
        const auto script = attachScript(f.scene.registry(), f.types, entity, "Recorder");
        REQUIRE(script);
        CHECK(dynamic_cast<Recorder*>(*script));
        REQUIRE(f.scene.registry().has<Scripts>(entity));
        const auto& entries = f.scene.registry().get<Scripts>(entity).entries;
        REQUIRE(entries.size() == 1);
        CHECK(entries[0].name == "Recorder");
        CHECK(entries[0].script.get() == *script);
    }

    TEST_CASE("attaches several scripts to one entity in order")
    {
        Fixture f;
        const auto entity = f.spawn("Recorder");
        REQUIRE(attachScript(f.scene.registry(), f.types, entity, "Mover"));
        const auto& entries = f.scene.registry().get<Scripts>(entity).entries;
        REQUIRE(entries.size() == 2);
        CHECK(entries[0].name == "Recorder");
        CHECK(entries[1].name == "Mover");
    }

    TEST_CASE("reports an unknown script name")
    {
        Fixture f;
        const auto entity = f.scene.registry().create();
        const auto script = attachScript(f.scene.registry(), f.types, entity, "Nope");
        REQUIRE_FALSE(script);
        CHECK(script.error().code == reflect::ErrorCode::UnknownType);
        CHECK(script.error().message.contains("Nope"));
        CHECK_FALSE(f.scene.registry().has<Scripts>(entity)); // Nothing attached
    }

    TEST_CASE("reports a name that is a component, not a script")
    {
        Fixture f;
        const auto entity = f.scene.registry().create();
        const auto script = attachScript(f.scene.registry(), f.types, entity, "Transform");
        REQUIRE_FALSE(script);
        CHECK(script.error().code == reflect::ErrorCode::WrongCategory);
    }

    TEST_CASE("reports a script that cannot be created by name")
    {
        Fixture f;
        const auto entity = f.scene.registry().create();
        const auto script = attachScript(f.scene.registry(), f.types, entity, "NoDefault");
        REQUIRE_FALSE(script);
        CHECK(script.error().code == reflect::ErrorCode::NotConstructible);
    }

    TEST_CASE("attaches a script whose Script base is not its first base")
    {
        Fixture f;
        const auto entity = f.spawn("MultiBase");
        f.scene.begin();
        f.scene.update(0.016f);
        CHECK(g_log == std::vector<std::string>{ "multi.update" });
        f.scene.registry().destroy(entity); // Destroys through the right pointer
    }

    TEST_CASE("refuses a script registered without its Script base")
    {
        Fixture f;
        const auto entity = f.scene.registry().create();
        const auto script = attachScript(f.scene.registry(), f.types, entity, "Orphan");
        REQUIRE_FALSE(script);
        CHECK(script.error().code == reflect::ErrorCode::WrongCategory);
        CHECK_FALSE(f.scene.registry().has<Scripts>(entity));
    }

    TEST_CASE("sets a script's registered fields before it runs")
    {
        Fixture f;
        const auto entity = f.spawn("Mover");
        auto* mover = dynamic_cast<Mover*>(f.scene.registry().get<Scripts>(entity).entries[0].script.get());
        REQUIRE(mover);
        CHECK(f.types.find("Mover")->setText(mover, "speed", "4"));

        f.scene.begin();
        f.scene.update(0.5f);
        CHECK(f.scene.registry().get<Transform>(entity).location.x == doctest::Approx(2.0f));
    }
}

TEST_SUITE("scripting::ScriptSystem")
{
    TEST_CASE("calls onCreate once at begin, then onUpdate every frame")
    {
        Fixture f;
        f.spawnRecorder("a");
        f.scene.begin();
        CHECK(g_log == std::vector<std::string>{ "a.create" });
        f.scene.update(0.016f);
        f.scene.update(0.016f);
        CHECK(g_log == std::vector<std::string>{ "a.create", "a.update", "a.update" });
    }

    TEST_CASE("creates a script before its first update when it starts after begin")
    {
        Fixture f;
        f.scene.begin();
        f.spawnRecorder("late");
        CHECK(g_log.empty());
        f.scene.update(0.016f);
        f.scene.update(0.016f);
        CHECK(g_log == std::vector<std::string>{ "late.create", "late.update", "late.update" });
    }

    TEST_CASE("runs the scripts of an entity in attach order")
    {
        Fixture f;
        const auto entity = f.spawnRecorder("first");
        auto second = attachScript(f.scene.registry(), f.types, entity, "Recorder");
        REQUIRE(second);
        static_cast<Recorder*>(*second)->tag = "second";

        f.scene.begin();
        f.scene.update(0.016f);
        CHECK(g_log == std::vector<std::string>{ "first.create", "second.create", "first.update", "second.update" });
        g_log.clear();
        f.scene.registry().destroy(entity);
        CHECK(g_log == std::vector<std::string>{ "first.destroy", "second.destroy" });
    }

    TEST_CASE("calls onDestroy when the entity is destroyed, while it still has its components")
    {
        Fixture f;
        const auto entity = f.spawnRecorder("a");
        f.scene.begin();
        g_log.clear();

        f.scene.registry().destroy(entity);
        CHECK(g_log == std::vector<std::string>{ "a.destroy" }); // No "!": Transform and Name were still there
        f.scene.update(0.016f);
        CHECK(g_log == std::vector<std::string>{ "a.destroy" }); // Not updated any more
    }

    TEST_CASE("does not call onDestroy for a script that was never created")
    {
        Fixture f;
        const auto entity = f.spawnRecorder("a");
        f.scene.registry().destroy(entity);
        CHECK(g_log.empty());
    }

    TEST_CASE("calls onDestroy for every script on reset, then removes the entities")
    {
        Fixture f;
        f.spawnRecorder("a");
        f.spawnRecorder("b");
        f.scene.registry().create("No scripts");
        f.scene.begin();
        g_log.clear();

        f.scene.reset();
        CHECK(g_log.size() == 2);
        CHECK(std::ranges::count(g_log, "a.destroy") == 1); // With the entity intact
        CHECK(std::ranges::count(g_log, "b.destroy") == 1);
        CHECK(f.scene.registry().entityCount() == 0);
    }

    TEST_CASE("does not end a script twice")
    {
        Fixture f;
        f.spawnRecorder("a");
        f.scene.begin();
        g_log.clear();

        f.scene.reset();
        f.scene.reset();
        CHECK(g_log == std::vector<std::string>{ "a.destroy" });
    }

    TEST_CASE("calls onDestroy when the scene is destroyed")
    {
        Fixture f;
        {
            World scene;
            scene.addSystem<ScriptSystem>(scene.registry());
            const auto entity = scene.registry().create("a");
            auto script = attachScript(scene.registry(), f.types, entity, "Recorder");
            REQUIRE(script);
            static_cast<Recorder*>(*script)->tag = "scoped";
            scene.begin();
            g_log.clear();
        }
        CHECK(g_log == std::vector<std::string>{ "scoped.destroy" });
    }

    TEST_CASE("a reset scene can be filled and run again")
    {
        Fixture f;
        f.spawnRecorder("old");
        f.scene.begin();
        f.scene.reset();
        g_log.clear();

        f.scene.addSystem<ScriptSystem>(f.scene.registry());
        f.spawnRecorder("new");
        f.scene.begin();
        f.scene.update(0.016f);
        CHECK(g_log == std::vector<std::string>{ "new.create", "new.update" });
    }

    TEST_CASE("a script can destroy its own entity during its update")
    {
        Fixture f;
        const auto entity = f.spawn("Suicide");
        f.scene.begin();
        f.scene.update(0.016f);
        CHECK(g_log == std::vector<std::string>{ "suicide.update", "suicide.destroy", "suicide.after" });
        CHECK_FALSE(f.scene.registry().valid(entity));
        f.scene.update(0.016f); // Nothing left to run
        CHECK(g_log.size() == 3);
    }

    TEST_CASE("a script can spawn an entity with a script")
    {
        Fixture f;
        const auto spawner = f.spawn("Spawner");
        auto* script = dynamic_cast<Spawner*>(f.scene.registry().get<Scripts>(spawner).entries[0].script.get());
        REQUIRE(script);
        script->types = &f.types;

        f.scene.begin();
        f.scene.update(0.016f); // Spawns
        f.scene.update(0.016f); // The new script starts
        CHECK(g_log == std::vector<std::string>{ "rec.create", "rec.update" });
    }

    TEST_CASE("gives scripts their entity and its components")
    {
        Fixture f;
        const auto a = f.spawn("Mover", "A");
        const auto b = f.spawn("Mover", "B");
        f.scene.registry().get<Transform>(b).location.x = 10.0f;
        f.scene.begin();
        f.scene.update(1.0f);
        CHECK(f.scene.registry().get<Transform>(a).location.x == doctest::Approx(1.0f));
        CHECK(f.scene.registry().get<Transform>(b).location.x == doctest::Approx(11.0f));
    }

    TEST_CASE("ends the scripts when the system goes away")
    {
        g_log.clear();
        TypeRegistry types;
        REQUIRE(registerScript<Recorder>(types, "Recorder"));
        World scene;
        scene.addSystem<ScriptSystem>(scene.registry());
        const auto entity = scene.registry().create();
        REQUIRE(attachScript(scene.registry(), types, entity, "Recorder"));
        scene.begin();
        scene.reset();
        CHECK(g_log == std::vector<std::string>{ "rec.create", "rec.destroy" });
        CHECK(scene.registry().entityCount() == 0);
    }
}

TEST_SUITE("scripting::ScriptSystem in edit mode")
{
    TEST_CASE("an edit world neither creates nor updates scripts")
    {
        Fixture f;
        f.spawnRecorder("a");
        f.scene.begin(world::UpdateMode::Edit);
        f.scene.update(0.016f, world::UpdateMode::Edit);
        CHECK(g_log.empty());
    }

    TEST_CASE("an edit world does not move an entity with a moving script")
    {
        Fixture f;
        const auto entity = f.spawn("Mover");
        f.scene.update(1.0f, world::UpdateMode::Edit);
        CHECK(f.scene.registry().get<Transform>(entity).location.x == doctest::Approx(0.0f));
    }
}
