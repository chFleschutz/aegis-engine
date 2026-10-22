#include <doctest/doctest.h>

import std;
import aegis.assets;
import aegis.math;
import aegis.reflect;
import aegis.scripting.script;
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
using aegis::serialize::EntityIds;
using aegis::serialize::ErrorCode;
using aegis::serialize::Scene;
using aegis::serialize::SceneInstance;
using aegis::world::Entity;
using aegis::world::Registry;

constexpr std::uint64_t propId = 0x00000000000000bb;
constexpr std::uint64_t snapshotId = 0x00000000000000ee;

constexpr std::uint64_t rootId = 0x1c5e09b7f3a8d264;
constexpr std::uint64_t lampId = 0x2d6f1ac804b9e375;
constexpr std::uint64_t bulbId = 0x7a21e4c90d3b5f86;
constexpr std::uint64_t crateId = 0x3b0f5e2a9c1d4e77;
constexpr std::uint64_t floorId = 0x4c1a6f3b0d2e5f88;

// A game's script
class Spinner : public aegis::scripting::Script
{
public:
    float speed{ 1.0f };
};

auto registerTypes(TypeRegistry& types) -> void
{
    REQUIRE(aegis::world::registerComponents(types).has_value());
    REQUIRE(aegis::serialize::registerComponents(types).has_value());
    auto spinner = aegis::scripting::registerScript<Spinner>(types, "Spinner");
    REQUIRE(spinner.has_value());
    REQUIRE(spinner->field("speed", &Spinner::speed).status().has_value());
}

auto entity(Registry& registry, std::uint64_t id) -> Entity
{
    const Entity found = registry.find(aegis::StableId{ id });
    REQUIRE(found);
    return found;
}

auto idOf(Registry& registry, Entity entity) -> std::uint64_t
{
    return registry.get<aegis::StableId>(entity).value;
}

auto children(Registry& registry, Entity parent) -> std::vector<std::uint64_t>
{
    std::vector<std::uint64_t> result;
    for (Entity child = registry.get<aegis::Children>(parent).first; child; child = registry.get<aegis::Siblings>(child).next)
        result.push_back(idOf(registry, child));
    return result;
}

auto ids(Registry& registry) -> std::set<std::uint64_t>
{
    std::set<std::uint64_t> result;
    for (const auto handle : registry.view<aegis::StableId>())
        result.insert(idOf(registry, Entity{ handle }));
    return result;
}

// Destroys `root` and its descendants
auto destroySubtree(Registry& registry, Entity root) -> void
{
    std::vector<Entity> pending{ root };
    std::vector<Entity> order;
    while (!pending.empty())
    {
        const Entity current = pending.back();
        pending.pop_back();
        order.push_back(current);
        for (Entity child = registry.get<aegis::Children>(current).first; child; child = registry.get<aegis::Siblings>(child).next)
            pending.push_back(child);
    }
    for (auto it = order.rbegin(); it != order.rend(); ++it)
        registry.destroy(*it);
}

// Root -> Floor, Lamp -> (Bulb with a script, Crate): the lamp is captured, the floor is its sibling
auto buildWorld(Registry& registry, const TypeRegistry& types) -> void
{
    const Entity root = registry.createWithId(rootId, "Root");
    const Entity floor = registry.createWithId(floorId, "Floor");
    const Entity lamp = registry.createWithId(lampId, "Lamp", { 0.0f, 0.0f, 1.5f });
    registry.add<aegis::PointLight>(lamp, glm::vec3{ 1.0f, 0.5f, 0.25f }, 200.0f);
    const Entity bulb = registry.createWithId(bulbId, "Bulb");
    registry.add<aegis::DynamicTag>(bulb);
    const Entity crate = registry.createWithId(crateId, "Crate");
    REQUIRE(registry.addChild(root, lamp));
    REQUIRE(registry.addChild(root, floor));
    REQUIRE(registry.addChild(lamp, bulb));
    REQUIRE(registry.addChild(lamp, crate));

    auto spinner = aegis::scripting::attachScript(registry, types, bulb, "Spinner");
    REQUIRE(spinner.has_value());
    static_cast<Spinner*>(*spinner)->speed = 2.5f;
}

auto capture(Registry& registry, const TypeRegistry& types, Entity root) -> Scene
{
    auto scene = aegis::serialize::saveScene(registry, types, root, snapshotId);
    REQUIRE(scene.has_value());
    return std::move(*scene);
}

// The scene `id` with one entity, whose ID is 1
auto propScene(const TypeRegistry& types) -> AssetRef<Scene>
{
    auto scene = aegis::serialize::parseScene(R"({
  "type": "Scene", "id": "00000000000000bb", "version": 1, "types": { "Transform": 1 },
  "entities": [{ "id": "0000000000000001", "name": "Prop" }]
})",
        types);
    REQUIRE(scene.has_value());
    return AssetRef<Scene>::ready(AssetId{ propId }, std::make_shared<Scene>(std::move(*scene)));
}

// Hands `prop` to the instances that name it, as the asset library does
auto prepareWith(const AssetRef<Scene>& prop) -> aegis::serialize::PrepareComponent
{
    return [prop](const aegis::reflect::TypeInfo& type, void* component) {
        if (type.type != typeid(SceneInstance))
            return;
        auto& instance = *static_cast<SceneInstance*>(component);
        if (instance.scene.id().value == propId)
            instance.scene = prop;
    };
}
}

TEST_SUITE("serialize::scene subtrees")
{
    TEST_CASE("captures an entity and its descendants with the entity as the root")
    {
        TypeRegistry types;
        registerTypes(types);
        Registry registry;
        buildWorld(registry, types);

        const Scene scene = capture(registry, types, entity(registry, lampId));
        CHECK(scene.id == snapshotId);
        REQUIRE(scene.entities.size() == 3);
        std::set<std::uint64_t> captured;
        for (const auto& saved : scene.entities)
            captured.insert(saved.id);
        CHECK(captured == std::set<std::uint64_t>{ lampId, bulbId, crateId });
        CHECK(std::ranges::is_sorted(scene.entities, {}, &aegis::serialize::SceneEntity::id));
    }

    TEST_CASE("restores a destroyed subtree with its IDs, components, scripts and place")
    {
        TypeRegistry types;
        registerTypes(types);
        Registry registry;
        buildWorld(registry, types);
        const Scene scene = capture(registry, types, entity(registry, lampId));
        const std::string before = aegis::serialize::writeScene(scene);

        destroySubtree(registry, entity(registry, lampId));
        CHECK(ids(registry) == std::set<std::uint64_t>{ rootId, floorId });

        const Entity root = entity(registry, rootId);
        auto lamp = aegis::serialize::instantiate(registry, types, scene, root, 0, EntityIds::Keep);
        REQUIRE(lamp.has_value());
        CHECK(idOf(registry, *lamp) == lampId);
        CHECK(children(registry, root) == std::vector<std::uint64_t>{ lampId, floorId });
        CHECK(children(registry, *lamp) == std::vector<std::uint64_t>{ bulbId, crateId });
        CHECK(registry.get<aegis::PointLight>(*lamp).intensity == 200.0f);
        CHECK(registry.get<aegis::Transform>(*lamp).location.z == 1.5f);

        const Entity bulb = entity(registry, bulbId);
        CHECK(registry.has<aegis::DynamicTag>(bulb));
        REQUIRE(registry.has<aegis::scripting::Scripts>(bulb));
        const auto& entries = registry.get<aegis::scripting::Scripts>(bulb).entries;
        REQUIRE(entries.size() == 1);
        CHECK(static_cast<Spinner*>(entries[0].script.get())->speed == 2.5f);

        CHECK(aegis::serialize::writeScene(capture(registry, types, *lamp)) == before);
    }

    TEST_CASE("gives every copy new IDs and its own entities")
    {
        TypeRegistry types;
        registerTypes(types);
        Registry registry;
        buildWorld(registry, types);
        const Scene scene = capture(registry, types, entity(registry, lampId));
        const Entity root = entity(registry, rootId);

        auto first = aegis::serialize::instantiate(registry, types, scene, root, 99, EntityIds::New);
        auto second = aegis::serialize::instantiate(registry, types, scene, root, 99, EntityIds::New);
        REQUIRE(first.has_value());
        REQUIRE(second.has_value());
        CHECK(registry.entityCount() == 11);

        std::set<std::uint64_t> seen{ rootId, floorId, lampId, bulbId, crateId };
        for (const Entity copy : { *first, *second })
        {
            CHECK(registry.get<aegis::Parent>(copy).entity == root);
            CHECK(registry.get<aegis::Name>(copy).name == "Lamp");
            const auto copyChildren = children(registry, copy);
            REQUIRE(copyChildren.size() == 2);
            CHECK(registry.get<aegis::Name>(entity(registry, copyChildren[0])).name == "Bulb");
            CHECK(registry.get<aegis::Name>(entity(registry, copyChildren[1])).name == "Crate");
            for (const std::uint64_t id : { idOf(registry, copy), copyChildren[0], copyChildren[1] })
                CHECK(seen.insert(id).second);
        }
        CHECK(children(registry, root) == std::vector<std::uint64_t>{ lampId, floorId, idOf(registry, *first), idOf(registry, *second) });

        // Changing one copy leaves the other and the original alone
        registry.get<aegis::PointLight>(*first).intensity = 5.0f;
        CHECK(registry.get<aegis::PointLight>(*second).intensity == 200.0f);
        CHECK(registry.get<aegis::PointLight>(entity(registry, lampId)).intensity == 200.0f);
        CHECK(children(registry, entity(registry, lampId)) == std::vector<std::uint64_t>{ bulbId, crateId });
    }

    TEST_CASE("inserts the root at the sibling index")
    {
        TypeRegistry types;
        registerTypes(types);
        Registry registry;
        buildWorld(registry, types);
        const Scene scene = capture(registry, types, entity(registry, lampId));
        const Entity root = entity(registry, rootId);

        auto middle = aegis::serialize::instantiate(registry, types, scene, root, 1, EntityIds::New);
        REQUIRE(middle.has_value());
        CHECK(children(registry, root) == std::vector<std::uint64_t>{ lampId, idOf(registry, *middle), floorId });

        auto unparented = aegis::serialize::instantiate(registry, types, scene, Entity{}, 0, EntityIds::New);
        REQUIRE(unparented.has_value());
        CHECK_FALSE(registry.get<aegis::Parent>(*unparented).entity);
    }

    TEST_CASE("recreates the instance of a holder in the subtree")
    {
        TypeRegistry types;
        registerTypes(types);
        const AssetRef<Scene> prop = propScene(types);
        Registry registry;
        buildWorld(registry, types);
        auto holder = aegis::serialize::instantiate(registry, types, prop, entity(registry, lampId), prepareWith(prop));
        REQUIRE(holder.has_value());
        const std::uint64_t holderId = idOf(registry, *holder);

        // The instance's entity is left out: the holder recreates it
        const Scene scene = capture(registry, types, entity(registry, lampId));
        CHECK(scene.entities.size() == 4);
        CHECK_FALSE(std::ranges::contains(scene.entities, deriveEntityId(holderId, 1), &aegis::serialize::SceneEntity::id));

        destroySubtree(registry, entity(registry, lampId));
        CHECK(ids(registry) == std::set<std::uint64_t>{ rootId, floorId });
        auto restored = aegis::serialize::instantiate(registry, types, scene, entity(registry, rootId), 0, EntityIds::Keep, prepareWith(prop));
        REQUIRE(restored.has_value());
        const Entity member = entity(registry, deriveEntityId(holderId, 1));
        CHECK(registry.get<aegis::Parent>(member).entity == entity(registry, holderId));
        CHECK(registry.get<aegis::serialize::InstanceMember>(member).holder == holderId);

        // A copy's holder has a new ID, so its instance's entities do too
        auto copy = aegis::serialize::instantiate(registry, types, scene, entity(registry, rootId), 99, EntityIds::New, prepareWith(prop));
        REQUIRE(copy.has_value());
        CHECK(registry.entityCount() == 2 + 2 * 5);
    }

    TEST_CASE("rejects a subtree inside a scene instance")
    {
        TypeRegistry types;
        registerTypes(types);
        const AssetRef<Scene> prop = propScene(types);
        Registry registry;
        auto holder = aegis::serialize::instantiate(registry, types, prop, Entity{}, prepareWith(prop));
        REQUIRE(holder.has_value());

        const Entity member = entity(registry, deriveEntityId(idOf(registry, *holder), 1));
        auto scene = aegis::serialize::saveScene(registry, types, member, snapshotId);
        REQUIRE_FALSE(scene.has_value());
        CHECK(scene.error().code == ErrorCode::InvalidData);
    }

    TEST_CASE("creates nothing when an ID it keeps is in use")
    {
        TypeRegistry types;
        registerTypes(types);
        Registry registry;
        buildWorld(registry, types);
        const Scene scene = capture(registry, types, entity(registry, lampId));
        registry.destroy(entity(registry, crateId));

        auto restored = aegis::serialize::instantiate(registry, types, scene, Entity{}, 0, EntityIds::Keep);
        REQUIRE_FALSE(restored.has_value());
        CHECK(restored.error().code == ErrorCode::DuplicateEntity);
        CHECK(ids(registry) == std::set<std::uint64_t>{ rootId, floorId, lampId, bulbId });
    }

    TEST_CASE("rejects a scene with more than one root")
    {
        TypeRegistry types;
        registerTypes(types);
        Registry registry;
        buildWorld(registry, types);
        auto world = aegis::serialize::saveScene(registry, types, snapshotId);
        REQUIRE(world.has_value());
        destroySubtree(registry, entity(registry, rootId));

        // The whole world has one root; without it, the floor and the lamp are two
        std::erase_if(world->entities, [](const auto& saved) { return saved.id == rootId; });
        auto restored = aegis::serialize::instantiate(registry, types, *world, Entity{}, 0, EntityIds::Keep);
        REQUIRE_FALSE(restored.has_value());
        CHECK(restored.error().code == ErrorCode::InvalidData);
        CHECK(registry.entityCount() == 0);
    }
}
