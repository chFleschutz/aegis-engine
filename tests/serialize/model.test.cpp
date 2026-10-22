#include <doctest/doctest.h>

import std;
import aegis.assets;
import aegis.math;
import aegis.reflect;
import aegis.serialize;
import aegis.world.registry;
import aegis.world.render_components;
import aegis.world.types;

namespace
{
using aegis::assets::AssetId;
using aegis::assets::AssetRef;
using aegis::assets::Model;
using aegis::assets::ModelGpuMesh;
using aegis::assets::ModelNode;
using aegis::assets::SubAssetKeys;
using aegis::reflect::TypeRegistry;
using aegis::serialize::ErrorCode;
using aegis::serialize::Scene;
using aegis::world::Entity;
using aegis::world::Registry;

constexpr std::uint64_t modelId = 0x00000000000000aa;

auto registerTypes(TypeRegistry& types) -> void
{
    REQUIRE(aegis::world::registerComponents(types).has_value());
}

// A mesh whose primitives draw the mesh and material IDs `primitives`
auto meshOf(std::string name, std::initializer_list<std::pair<std::uint64_t, std::uint64_t>> primitives) -> ModelGpuMesh
{
    ModelGpuMesh mesh{ .name = std::move(name), .primitives = {} };
    for (const auto& [meshId, materialId] : primitives)
    {
        mesh.primitives.push_back({
            .mesh = AssetRef<aegis::assets::StaticMesh>{ AssetId{ meshId } },
            .material = AssetRef<aegis::assets::Material>{ AssetId{ materialId } },
        });
    }
    return mesh;
}

// Entity IDs by key, numbered in the order modelScene asks for them, as a sidecar records them
class EntityIds
{
public:
    auto callback() -> aegis::serialize::ModelEntityId
    {
        return [this](const std::string& key) {
            const auto [it, inserted] = m_ids.try_emplace(key, m_ids.size() + 1);
            return it->second;
        };
    }

    auto at(const std::string& key) const -> std::uint64_t { return m_ids.at(key); }

    auto keys() const -> std::set<std::string>
    {
        std::set<std::string> keys;
        for (const auto& [key, id] : m_ids)
            keys.insert(key);
        return keys;
    }

private:
    std::map<std::string, std::uint64_t> m_ids;
};

auto modelScene(const Model& model, const TypeRegistry& types, EntityIds& ids, bool strictNames = false)
    -> std::expected<Scene, aegis::serialize::Error>
{
    return aegis::serialize::modelScene(model, modelId, types, ids.callback(), strictNames);
}

// The entities of `scene` in `registry`, with their local IDs
auto instantiated(Registry& registry, const TypeRegistry& types, const Scene& scene) -> void
{
    REQUIRE(aegis::serialize::instantiate(registry, types, scene).has_value());
}

// Exactly equal vectors or quaternions, of any glm precision (the file's numbers read back exactly)
template<class A, class B>
auto same(const A& a, const B& b) -> bool
{
    for (int i = 0; i < A::length(); ++i)
    {
        if (a[i] != b[i])
            return false;
    }
    return true;
}

auto entity(Registry& registry, std::uint64_t id) -> Entity
{
    const Entity found = registry.find(aegis::StableId{ id });
    REQUIRE(found);
    return found;
}

auto childIds(Registry& registry, Entity parent) -> std::vector<std::uint64_t>
{
    std::vector<std::uint64_t> ids;
    for (Entity child = registry.get<aegis::Children>(parent).first; child; child = registry.get<aegis::Siblings>(child).next)
        ids.push_back(registry.get<aegis::StableId>(child).value);
    return ids;
}
}

TEST_SUITE("serialize::modelScene")
{
    TEST_CASE("gives the root the model's name and rotation and puts the nodes below it")
    {
        TypeRegistry types;
        registerTypes(types);
        const glm::quat upAxis = glm::angleAxis(glm::radians(90.0f), glm::vec3{ 1.0f, 0.0f, 0.0f });
        Model model{ .name = "Helmet", .rootRotation = upAxis };
        model.nodes = {
            ModelNode{ .name = "Body", .translation = { 1.0f, 2.0f, 3.0f }, .scale = { 2.0f, 2.0f, 2.0f } },
            ModelNode{ .name = "Visor", .rotation = upAxis, .parent = 0 },
            ModelNode{ .name = "Stand" },
        };

        EntityIds ids;
        auto scene = modelScene(model, types, ids);
        REQUIRE(scene.has_value());
        CHECK(scene->id == modelId);
        CHECK(scene->entities.size() == 4);

        Registry registry;
        instantiated(registry, types, *scene);
        const Entity root = entity(registry, ids.at(std::string{ SubAssetKeys::rootEntity }));
        CHECK(registry.get<aegis::Name>(root).name == "Helmet");
        CHECK(same(registry.get<aegis::Transform>(root).rotation, upAxis));
        CHECK_FALSE(registry.get<aegis::Parent>(root).entity);
        CHECK(childIds(registry, root) == std::vector{ ids.at("Entity/Body"), ids.at("Entity/Stand") });

        const Entity body = entity(registry, ids.at("Entity/Body"));
        CHECK(registry.get<aegis::Name>(body).name == "Body");
        CHECK(same(registry.get<aegis::Transform>(body).location, glm::vec3{ 1.0f, 2.0f, 3.0f }));
        CHECK(same(registry.get<aegis::Transform>(body).scale, glm::vec3{ 2.0f, 2.0f, 2.0f }));
        CHECK(childIds(registry, body) == std::vector{ ids.at("Entity/Visor") });
        CHECK(same(registry.get<aegis::Transform>(entity(registry, ids.at("Entity/Visor"))).rotation, upAxis));
        CHECK_FALSE(registry.has<aegis::MeshRenderer>(body));
    }

    TEST_CASE("draws a mesh of one primitive on its node")
    {
        TypeRegistry types;
        registerTypes(types);
        Model model{ .name = "Model" };
        model.meshes = { meshOf("Body", { { 0x10, 0x20 } }) };
        model.nodes = { ModelNode{ .name = "Body", .mesh = 0 } };

        EntityIds ids;
        auto scene = modelScene(model, types, ids);
        REQUIRE(scene.has_value());
        CHECK(scene->entities.size() == 2);

        Registry registry;
        instantiated(registry, types, *scene);
        const auto& renderer = registry.get<aegis::MeshRenderer>(entity(registry, ids.at("Entity/Body")));
        CHECK(renderer.mesh.id() == AssetId{ 0x10 });
        CHECK(renderer.material.id() == AssetId{ 0x20 });
    }

    TEST_CASE("draws the primitives of a mesh on child entities of its node")
    {
        TypeRegistry types;
        registerTypes(types);
        Model model{ .name = "Model" };
        model.meshes = { meshOf("Body", { { 0x10, 0x20 }, { 0x11, 0x21 } }) };
        model.nodes = { ModelNode{ .name = "Body", .mesh = 0 }, ModelNode{ .name = "Visor", .parent = 0 } };

        EntityIds ids;
        auto scene = modelScene(model, types, ids);
        REQUIRE(scene.has_value());

        Registry registry;
        instantiated(registry, types, *scene);
        const Entity body = entity(registry, ids.at("Entity/Body"));
        CHECK_FALSE(registry.has<aegis::MeshRenderer>(body));
        CHECK(childIds(registry, body) == std::vector{ ids.at("Entity/Body/0"), ids.at("Entity/Body/1"), ids.at("Entity/Visor") });
        for (std::uint64_t sub = 0; sub < 2; ++sub)
        {
            const Entity primitive = entity(registry, ids.at(std::format("Entity/Body/{}", sub)));
            CHECK(registry.get<aegis::Name>(primitive).name == std::format("Body_Submesh_{}", sub));
            CHECK(registry.get<aegis::MeshRenderer>(primitive).mesh.id() == AssetId{ 0x10 + sub });
            CHECK(registry.get<aegis::MeshRenderer>(primitive).material.id() == AssetId{ 0x20 + sub });
        }
    }

    TEST_CASE("draws the root mesh of a model without nodes on the root")
    {
        TypeRegistry types;
        registerTypes(types);
        Model model{ .name = "cube", .rootMesh = 0 };
        model.meshes = { meshOf("cube", { { 0x10, 0x20 } }) };

        EntityIds ids;
        auto scene = modelScene(model, types, ids);
        REQUIRE(scene.has_value());
        REQUIRE(scene->entities.size() == 1);
        CHECK(ids.keys() == std::set<std::string>{ std::string{ SubAssetKeys::rootEntity } });

        Registry registry;
        instantiated(registry, types, *scene);
        CHECK(registry.get<aegis::MeshRenderer>(entity(registry, ids.at(std::string{ SubAssetKeys::rootEntity }))).mesh.id() == AssetId{ 0x10 });
    }

    TEST_CASE("keys entities by node name like sub-assets")
    {
        TypeRegistry types;
        registerTypes(types);
        Model model{ .name = "Model" };
        model.nodes = { ModelNode{ .name = "Wheel" }, ModelNode{ .name = "" }, ModelNode{ .name = "Wheel" }, ModelNode{ .name = "A/B" } };

        EntityIds ids;
        REQUIRE(modelScene(model, types, ids).has_value());
        CHECK(ids.keys() == std::set<std::string>{ "Entity/#root", "Entity/Wheel", "Entity/#1", "Entity/Wheel#1", "Entity/A\\/B" });
        for (const auto& key : ids.keys())
            CHECK(SubAssetKeys::isEntityKey(key));
    }

    TEST_CASE("gives the same scene for the same entity IDs")
    {
        TypeRegistry types;
        registerTypes(types);
        Model model{ .name = "Model" };
        model.meshes = { meshOf("Body", { { 0x10, 0x20 }, { 0x11, 0x21 } }) };
        model.nodes = { ModelNode{ .name = "Body", .mesh = 0 }, ModelNode{ .name = "Visor", .parent = 0 } };

        EntityIds first, second;
        auto a = modelScene(model, types, first);
        auto b = modelScene(model, types, second);
        REQUIRE(a.has_value());
        REQUIRE(b.has_value());
        CHECK(aegis::serialize::writeScene(*a) == aegis::serialize::writeScene(*b));

        // Its scene file reads back as the same scene
        auto read = aegis::serialize::parseScene(aegis::serialize::writeScene(*a), types);
        REQUIRE(read.has_value());
        CHECK(aegis::serialize::writeScene(*read) == aegis::serialize::writeScene(*a));
    }

    TEST_CASE("fails on an unnamed node with strictNames")
    {
        TypeRegistry types;
        registerTypes(types);
        Model model{ .name = "Model" };
        model.nodes = { ModelNode{ .name = "" } };

        EntityIds ids;
        auto scene = modelScene(model, types, ids, true);
        REQUIRE_FALSE(scene.has_value());
        CHECK(scene.error().code == ErrorCode::InvalidData);
    }

    TEST_CASE("fails without Transform and MeshRenderer registered")
    {
        TypeRegistry types;
        EntityIds ids;
        auto scene = modelScene(Model{ .name = "Model" }, types, ids);
        REQUIRE_FALSE(scene.has_value());
        CHECK(scene.error().code == ErrorCode::UnknownType);
    }

    TEST_CASE("fails when two entities get one ID")
    {
        TypeRegistry types;
        registerTypes(types);
        Model model{ .name = "Model" };
        model.nodes = { ModelNode{ .name = "A" } };

        auto scene = aegis::serialize::modelScene(model, modelId, types, [](const std::string&) { return std::uint64_t{ 7 }; });
        REQUIRE_FALSE(scene.has_value());
        CHECK(scene.error().code == ErrorCode::DuplicateEntity);
    }
}
