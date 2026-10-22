#include <doctest/doctest.h>

import std;
import aegis.assets;
import aegis.core;
import aegis.reflect;
import aegis.serialize;
import aegis.world.registry;
import aegis.world.render_components;
import aegis.world.types;

namespace
{
using aegis::assets::AssetId;
using aegis::assets::AssetRef;
using aegis::reflect::TypeRegistry;
using aegis::serialize::deriveEntityId;
using aegis::serialize::ErrorCode;
using aegis::serialize::Scene;
using aegis::serialize::SceneInstance;
using aegis::world::Entity;
using aegis::world::Registry;

constexpr std::uint64_t levelId = 0x00000000000000aa;
constexpr std::uint64_t propId = 0x00000000000000bb;
constexpr std::uint64_t lampId = 0x00000000000000cc;

auto registerTypes(TypeRegistry& types) -> void
{
    REQUIRE(aegis::world::registerComponents(types).has_value());
    REQUIRE(aegis::serialize::registerComponents(types).has_value());
}

// A scene file `id` with `entities` (the text of the entity list)
auto sceneText(std::uint64_t id, std::string_view entities) -> std::string
{
    return std::format(R"({{
  "type": "Scene",
  "id": "{:016x}",
  "version": 1,
  "types": {{ "SceneInstance": 1, "Sky": 1, "Transform": 1 }},
  "entities": [
{}
  ]
}}
)",
        id, entities);
}

auto entityText(std::uint64_t id, std::string_view rest = {}) -> std::string
{
    return std::format(R"(    {{ "id": "{:016x}", "name": "E"{} }},)", id, rest);
}

// An entity holding an instance of the scene `scene`
auto holderText(std::uint64_t id, std::uint64_t scene, std::string_view rest = {}) -> std::string
{
    return entityText(id, std::format(R"(, "components": {{ "SceneInstance": {{ "scene": "{:016x}" }} }}{})", scene, rest));
}

// The scene files of a test, parsed, which `prepare` hands to the instances that name them as the asset
// library does
class Scenes
{
public:
    explicit Scenes(const TypeRegistry& types) :
        m_types{ types }
    {
    }

    auto add(std::uint64_t id, std::string_view entities) -> const Scene&
    {
        auto scene = aegis::serialize::parseScene(sceneText(id, entities), m_types);
        REQUIRE(scene.has_value());
        auto shared = std::make_shared<Scene>(std::move(*scene));
        m_refs.insert_or_assign(id, AssetRef<Scene>::ready(AssetId{ id }, shared));
        return *shared;
    }

    auto ref(std::uint64_t id) const -> AssetRef<Scene> { return m_refs.at(id); }

    auto prepare() const -> aegis::serialize::PrepareComponent
    {
        return [this](const aegis::reflect::TypeInfo& type, void* component) {
            if (type.type != typeid(SceneInstance))
                return;
            auto& instance = *static_cast<SceneInstance*>(component);
            if (auto it = m_refs.find(instance.scene.id().value); it != m_refs.end())
                instance.scene = it->second;
        };
    }

private:
    const TypeRegistry& m_types;
    std::map<std::uint64_t, AssetRef<Scene>> m_refs;
};

auto ids(Registry& registry) -> std::set<std::uint64_t>
{
    std::set<std::uint64_t> result;
    for (const auto handle : registry.view<aegis::StableId>())
        result.insert(registry.get<aegis::StableId>(Entity{ handle }).value);
    return result;
}

auto entity(Registry& registry, std::uint64_t id) -> Entity
{
    const Entity found = registry.find(aegis::StableId{ id });
    REQUIRE(found);
    return found;
}

// Saves `registry` as the scene `id` and reads the file back
auto roundTrip(Registry& registry, const TypeRegistry& types, std::uint64_t id) -> Scene
{
    auto saved = aegis::serialize::saveScene(registry, types, id);
    REQUIRE(saved.has_value());
    auto read = aegis::serialize::parseScene(aegis::serialize::writeScene(*saved), types);
    REQUIRE(read.has_value());
    return std::move(*read);
}
}

TEST_SUITE("serialize::deriveEntityId")
{
    // Saved games and overrides name instance entities by these IDs: a change here breaks them
    TEST_CASE("gives the pinned XXH3-64 of both IDs as little-endian bytes")
    {
        CHECK(deriveEntityId(0x0000000000000001, 0x0000000000000001) == 0x6ca7f978e15828a6);
        CHECK(deriveEntityId(0x1c5e09b7f3a8d264, 0x7a21e4c90d3b5f86) == 0x80d62ee16e29c7bd);
        CHECK(deriveEntityId(0x7a21e4c90d3b5f86, 0x1c5e09b7f3a8d264) == 0xc172bc9ea8851b07);
    }

    TEST_CASE("chains for nested instances")
    {
        CHECK(deriveEntityId(deriveEntityId(0x1c5e09b7f3a8d264, 0x3b0f5e2a9c1d4e77), 0x7a21e4c90d3b5f86) == 0x040e953777708e63);
    }
}

TEST_SUITE("serialize::scene instances")
{
    TEST_CASE("gives each instance of a scene its own derived IDs")
    {
        TypeRegistry types;
        registerTypes(types);
        Scenes scenes{ types };
        scenes.add(propId, entityText(1, R"(, "children": ["0000000000000002"])") + "\n" + entityText(2));

        Registry registry;
        auto first = aegis::serialize::instantiate(registry, types, scenes.ref(propId), Entity{}, scenes.prepare());
        auto second = aegis::serialize::instantiate(registry, types, scenes.ref(propId), Entity{}, scenes.prepare());
        REQUIRE(first.has_value());
        REQUIRE(second.has_value());
        CHECK(registry.entityCount() == 6);

        for (const Entity holder : { *first, *second })
        {
            const std::uint64_t holderId = registry.get<aegis::StableId>(holder).value;
            const Entity root = entity(registry, deriveEntityId(holderId, 1));
            const Entity child = entity(registry, deriveEntityId(holderId, 2));
            CHECK(registry.get<aegis::Parent>(root).entity == holder);
            CHECK(registry.get<aegis::Parent>(child).entity == root);
            CHECK(registry.get<aegis::serialize::InstanceMember>(child).holder == holderId);
            CHECK(registry.get<SceneInstance>(holder).scene.id().value == propId);
        }
    }

    TEST_CASE("puts an instance under its parent")
    {
        TypeRegistry types;
        registerTypes(types);
        Scenes scenes{ types };
        scenes.add(propId, entityText(1));

        Registry registry;
        const Entity parent = registry.create("Parent");
        auto holder = aegis::serialize::instantiate(registry, types, scenes.ref(propId), parent, scenes.prepare());
        REQUIRE(holder.has_value());
        CHECK(registry.get<aegis::Parent>(*holder).entity == parent);
    }

    TEST_CASE("keeps instance entity IDs through save and load")
    {
        TypeRegistry types;
        registerTypes(types);
        Scenes scenes{ types };
        scenes.add(propId, entityText(1, R"(, "children": ["0000000000000002"])") + "\n" + entityText(2));
        const Scene& level = scenes.add(levelId, holderText(0x10, propId) + "\n" + holderText(0x20, propId));

        Registry registry;
        REQUIRE(aegis::serialize::instantiate(registry, types, level, scenes.prepare()).has_value());
        const auto before = ids(registry);
        CHECK(before == std::set<std::uint64_t>{ 0x10, 0x20, deriveEntityId(0x10, 1), deriveEntityId(0x10, 2), deriveEntityId(0x20, 1), deriveEntityId(0x20, 2) });

        // Only the holders are saved; loading the file recreates their instances with the same IDs
        const Scene saved = roundTrip(registry, types, levelId);
        CHECK(saved.entities.size() == 2);
        Registry loaded;
        REQUIRE(aegis::serialize::instantiate(loaded, types, saved, scenes.prepare()).has_value());
        CHECK(ids(loaded) == before);
    }

    TEST_CASE("round-trips a nested instance through a scene file")
    {
        TypeRegistry types;
        registerTypes(types);
        Scenes scenes{ types };
        scenes.add(lampId, entityText(3));
        scenes.add(propId, holderText(1, lampId) + "\n" + entityText(2));
        const Scene& level = scenes.add(levelId, holderText(0x10, propId, R"(, "children": ["0000000000000011"])") + "\n" + entityText(0x11));

        Registry registry;
        REQUIRE(aegis::serialize::instantiate(registry, types, level, scenes.prepare()).has_value());
        const std::uint64_t propHolder = deriveEntityId(0x10, 1);
        const std::uint64_t bulb = deriveEntityId(propHolder, 3);
        CHECK(ids(registry) == std::set<std::uint64_t>{ 0x10, 0x11, propHolder, deriveEntityId(0x10, 2), bulb });
        CHECK(registry.get<aegis::Parent>(entity(registry, bulb)).entity == entity(registry, propHolder));
        CHECK(registry.get<aegis::serialize::InstanceMember>(entity(registry, bulb)).holder == propHolder);

        // The level's own child comes before the instance's roots
        const Entity holder = entity(registry, 0x10);
        CHECK(registry.get<aegis::Children>(holder).first == entity(registry, 0x11));

        const Scene saved = roundTrip(registry, types, levelId);
        REQUIRE(saved.entities.size() == 2);
        CHECK(saved.entities[0].children == std::vector<std::uint64_t>{ 0x11 });
        CHECK(saved.entities[0].components.front().type->name == "SceneInstance");

        Registry loaded;
        REQUIRE(aegis::serialize::instantiate(loaded, types, saved, scenes.prepare()).has_value());
        CHECK(ids(loaded) == ids(registry));
    }

    TEST_CASE("fails on a scene that holds itself, with the chain")
    {
        TypeRegistry types;
        registerTypes(types);
        Scenes scenes{ types };
        scenes.add(propId, holderText(1, lampId));
        scenes.add(lampId, holderText(2, propId));
        const Scene& level = scenes.add(levelId, holderText(0x10, propId));

        Registry registry;
        auto result = aegis::serialize::instantiate(registry, types, level, scenes.prepare());
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code == ErrorCode::Cycle);
        CHECK(result.error().message.contains("00000000000000bb -> 00000000000000cc -> 00000000000000bb"));
        CHECK(registry.entityCount() == 0);
    }

    TEST_CASE("fails on a level that holds itself")
    {
        TypeRegistry types;
        registerTypes(types);
        Scenes scenes{ types };
        const Scene& level = scenes.add(levelId, holderText(0x10, levelId));

        Registry registry;
        auto result = aegis::serialize::instantiate(registry, types, level, scenes.prepare());
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code == ErrorCode::Cycle);
        CHECK(result.error().message.contains("00000000000000aa -> 00000000000000aa"));
        CHECK(registry.entityCount() == 0);
    }

    TEST_CASE("fails on an instance whose scene did not load")
    {
        TypeRegistry types;
        registerTypes(types);
        Scenes scenes{ types };
        const Scene& level = scenes.add(levelId, entityText(0x01) + "\n" + holderText(0x10, propId));

        Registry registry;
        auto result = aegis::serialize::instantiate(registry, types, level, scenes.prepare());
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code == ErrorCode::MissingScene);
        CHECK(result.error().line == 8); // The holder's
        CHECK(registry.entityCount() == 0);
    }

    TEST_CASE("keeps the world's first singleton and warns about the others")
    {
        std::vector<std::string> warnings;
        aegis::log::setSink([&](aegis::log::Level level, std::string_view message) {
            if (level == aegis::log::Level::Warn)
                warnings.emplace_back(message);
        });

        TypeRegistry types;
        registerTypes(types);
        Scenes scenes{ types };
        scenes.add(propId, entityText(1, R"(, "components": { "Sky": {} })"));
        const Scene& level = scenes.add(levelId, entityText(0x01, R"(, "components": { "Sky": {} })") + "\n" + holderText(0x10, propId));

        Registry registry;
        const bool created = aegis::serialize::instantiate(registry, types, level, scenes.prepare()).has_value();
        aegis::log::setSink({});
        REQUIRE(created);
        CHECK(registry.has<aegis::Sky>(entity(registry, 0x01)));
        CHECK_FALSE(registry.has<aegis::Sky>(entity(registry, deriveEntityId(0x10, 1))));
        REQUIRE(warnings.size() == 1);
        CHECK(warnings.front().contains("Sky"));
    }
}
