#include <doctest/doctest.h>

import std;
import aegis.assets;
import aegis.reflect;
import aegis.scripting;
import aegis.serialize;
import aegis.world;

// ScriptContext::spawn, find, emit and destroy together, in a headless world: a Spawner over serialize::instantiate
// stands in for the engine's, which instantiates through the asset library the same way.
namespace
{
using aegis::StableId;
using aegis::Transform;
using aegis::assets::AssetId;
using aegis::assets::AssetRef;
using aegis::reflect::TypeRegistry;
using aegis::scripting::ScriptContext;
using aegis::serialize::Scene;
using aegis::world::Entity;
using aegis::world::World;

constexpr std::uint64_t prefabId = 0x00000000000000bb;
constexpr std::uint64_t lampId = 0x00000000000000cc; // The prefab's one entity

// Instantiates the scenes it was given, by ID
class SceneSpawner final : public aegis::scripting::Spawner
{
public:
    explicit SceneSpawner(const TypeRegistry& types) :
        m_types{ types }
    {
    }

    void add(AssetRef<Scene> scene) { m_scenes.insert_or_assign(scene.id().value, std::move(scene)); }

    auto spawn(World& world, AssetId scene, Entity parent, std::uint64_t holderId) -> std::expected<Entity, std::string> override
    {
        const auto found = m_scenes.find(scene.value);
        if (found == m_scenes.end())
            return std::unexpected{ std::format("no scene {}", scene) };
        auto holder = aegis::serialize::instantiate(world.registry(), m_types, found->second, parent, {}, holderId);
        if (!holder)
            return std::unexpected{ holder.error().message };
        ++spawned;
        return *holder;
    }

    int spawned{ 0 };

private:
    const TypeRegistry& m_types;
    std::unordered_map<std::uint64_t, AssetRef<Scene>> m_scenes;
};

// What PrefabSpawner tells the world it spawned
struct Spawned
{
    StableId holder;
};

// Spawns its prefab field as its own child in its first update and announces it; destroys it when asked
struct PrefabSpawner : aegis::scripting::Script
{
    AssetRef<Scene> prefab;
    StableId holder;
    bool destroyNow{ false };

    void onUpdate(ScriptContext& context, float) override
    {
        if (holder == StableId{})
        {
            holder = context.spawn(prefab, context.entity(), Transform{ .location = { 1.0f, 2.0f, 3.0f } });
            context.emit(Spawned{ holder });
        }
        if (destroyNow)
        {
            context.destroy(context.find(holder));
            destroyNow = false;
        }
    }
};

// Finds what the Spawned events name
struct Finder : aegis::scripting::Script
{
    std::vector<Entity> found;

    void onUpdate(ScriptContext& context, float) override
    {
        for (const auto& event : context.events<Spawned>())
            found.push_back(context.find(event.holder));
    }
};

auto prefabText() -> std::string
{
    return std::format(R"({{
  "type": "Scene",
  "id": "{:016x}",
  "version": 1,
  "types": {{ "Transform": 1 }},
  "entities": [
    {{ "id": "{:016x}", "name": "Lamp" }}
  ]
}}
)",
        prefabId, lampId);
}

template<typename T>
auto scriptOf(aegis::world::Registry& registry, Entity entity) -> T&
{
    return *dynamic_cast<T*>(registry.get<aegis::scripting::Scripts>(entity).entries[0].script.get());
}

struct Fixture
{
    Fixture()
    {
        REQUIRE(aegis::world::registerComponents(types).has_value());
        REQUIRE(aegis::serialize::registerComponents(types).has_value());
        REQUIRE(aegis::scripting::registerScript<PrefabSpawner>(types, "PrefabSpawner")->field("prefab", &PrefabSpawner::prefab).status());
        REQUIRE(aegis::scripting::registerScript<Finder>(types, "Finder"));

        auto parsed = aegis::serialize::parseScene(prefabText(), types);
        REQUIRE(parsed.has_value());
        spawner.add(AssetRef<Scene>::ready(AssetId{ prefabId }, std::make_shared<Scene>(std::move(*parsed))));

        world.addSystem<aegis::scripting::ScriptSystem>();
        world.registry().setContext<aegis::scripting::Spawner>(spawner);

        auto& registry = world.registry();
        spawnerEntity = registry.create("Spawner");
        REQUIRE(aegis::scripting::attachScript(registry, types, spawnerEntity, "PrefabSpawner"));
        finderEntity = registry.create("Finder");
        REQUIRE(aegis::scripting::attachScript(registry, types, finderEntity, "Finder"));

        // The prefab comes from the script's registered field, as a scene file sets it
        auto& script = scriptOf<PrefabSpawner>(registry, spawnerEntity);
        const auto* type = types.find("PrefabSpawner");
        REQUIRE(type);
        REQUIRE(type->setText(&script, "prefab", std::format("{:016x}", prefabId)));
        REQUIRE(script.prefab.id() == AssetId{ prefabId });
    }

    TypeRegistry types;
    SceneSpawner spawner{ types };
    World world;
    Entity spawnerEntity;
    Entity finderEntity;
};
}

TEST_SUITE("scripting::ScriptContext spawning")
{
    TEST_CASE("a script spawns its prefab field, the instance exists after the next update, and another script finds it by the ID it announced")
    {
        Fixture f;
        auto& registry = f.world.registry();

        f.world.update(0.016f); // Spawns (deferred) and emits
        const auto holderId = scriptOf<PrefabSpawner>(registry, f.spawnerEntity).holder;
        REQUIRE(holderId != StableId{});
        CHECK_FALSE(registry.find(holderId)); // Not yet
        CHECK(f.spawner.spawned == 0);

        f.world.update(0.016f); // Instantiates before the scripts run; they read the event
        const auto holder = registry.find(holderId);
        REQUIRE(holder);
        CHECK(f.spawner.spawned == 1);
        CHECK(registry.get<aegis::Parent>(holder).entity == f.spawnerEntity);
        CHECK(registry.get<Transform>(holder).location == aegis::math::Float3{ 1.0f, 2.0f, 3.0f });
        const auto lamp = registry.find(StableId{ aegis::serialize::deriveEntityId(holderId.value, lampId) });
        REQUIRE(lamp);
        CHECK(registry.get<aegis::Parent>(lamp).entity == holder);

        CHECK(scriptOf<Finder>(registry, f.finderEntity).found == std::vector<Entity>{ holder });
    }

    TEST_CASE("destroying the holder destroys the instance")
    {
        Fixture f;
        auto& registry = f.world.registry();
        f.world.update(0.016f);
        f.world.update(0.016f);
        auto& script = scriptOf<PrefabSpawner>(registry, f.spawnerEntity);
        const auto holder = registry.find(script.holder);
        const auto lamp = registry.find(StableId{ aegis::serialize::deriveEntityId(script.holder.value, lampId) });
        REQUIRE(holder);
        REQUIRE(lamp);

        script.destroyNow = true;
        f.world.update(0.016f); // Asks
        CHECK(registry.valid(holder));
        f.world.update(0.016f); // Destroys
        CHECK_FALSE(registry.valid(holder));
        CHECK_FALSE(registry.valid(lamp));
        CHECK(registry.valid(f.spawnerEntity));
    }

    TEST_CASE("a spawn whose parent is destroyed before it runs creates nothing")
    {
        Fixture f;
        auto& registry = f.world.registry();
        const auto before = registry.entityCount();
        f.world.update(0.016f);
        registry.destroy(f.spawnerEntity);
        f.world.update(0.016f);
        CHECK(f.spawner.spawned == 0);
        CHECK(registry.entityCount() == before - 1);
    }

    TEST_CASE("serialize::instantiate refuses a holder ID already in use")
    {
        Fixture f;
        auto& registry = f.world.registry();
        const auto taken = registry.get<StableId>(f.finderEntity).value;
        auto parsed = aegis::serialize::parseScene(prefabText(), f.types);
        REQUIRE(parsed.has_value());
        const auto scene = AssetRef<Scene>::ready(AssetId{ prefabId }, std::make_shared<Scene>(std::move(*parsed)));
        const auto count = registry.entityCount();
        const auto holder = aegis::serialize::instantiate(registry, f.types, scene, {}, {}, taken);
        REQUIRE_FALSE(holder);
        CHECK(holder.error().code == aegis::serialize::ErrorCode::DuplicateEntity);
        CHECK(registry.entityCount() == count);
    }
}
