#include <doctest/doctest.h>

import std;
import aegis.reflect;
import aegis.world.registry;
import aegis.world.render_components;
import aegis.world.types;

namespace
{
using aegis::reflect::FieldKind;
using aegis::reflect::TypeRegistry;
using aegis::world::ComponentStorage;
using aegis::world::Registry;

struct Health
{
    float value{ 100.0f };
};

auto storageOf(const TypeRegistry& types, std::string_view name) -> const ComponentStorage&
{
    const auto* info = types.find(name);
    REQUIRE(info != nullptr);
    const auto* storage = info->attachment<ComponentStorage>();
    REQUIRE(storage != nullptr);
    return *storage;
}
}

TEST_SUITE("world::registerComponents")
{
    TEST_CASE("registers the built-in components with their storage")
    {
        TypeRegistry types;
        REQUIRE(aegis::world::registerComponents(types).has_value());
        for (auto name : { "Transform", "DynamicTag", "MainCamera", "Camera", "AmbientLight", "DirectionalLight", "PointLight", "MeshRenderer", "Sky" })
        {
            CAPTURE(name);
            const auto* info = types.find(name);
            REQUIRE(info != nullptr);
            CHECK(info->category == aegis::reflect::TypeCategory::Component);
            CHECK(info->attachment<ComponentStorage>() != nullptr);
        }
    }

    TEST_CASE("leaves out what an entity's id, name and children hold or the engine computes")
    {
        TypeRegistry types;
        REQUIRE(aegis::world::registerComponents(types).has_value());
        const auto left = std::to_array<std::type_index>({ typeid(aegis::Name), typeid(aegis::StableId), typeid(aegis::Parent),
            typeid(aegis::Siblings), typeid(aegis::Children), typeid(aegis::GlobalTransform), typeid(aegis::StaticDirtyTag) });
        for (auto type : left)
        {
            CAPTURE(type.name());
            CHECK(types.find(type) == nullptr);
        }
    }

    TEST_CASE("names the fields after the C++ members")
    {
        TypeRegistry types;
        REQUIRE(aegis::world::registerComponents(types).has_value());
        const auto* transform = types.find<aegis::Transform>();
        REQUIRE(transform->fields.size() == 3);
        CHECK(transform->fields[0].name == "location");
        CHECK(transform->fields[1].kind == FieldKind::Quat);
        CHECK(types.find<aegis::MeshRenderer>()->findField("mesh")->kind == FieldKind::AssetRef);
        CHECK(types.find<aegis::Sky>()->findField("environment")->kind == FieldKind::AssetRef);
        CHECK(types.find<aegis::Camera>()->findField("aspect") == nullptr);
    }

    TEST_CASE("fails when a built-in name is taken")
    {
        TypeRegistry types;
        REQUIRE(types.component<Health>("PointLight").has_value());
        CHECK(aegis::world::registerComponents(types).error().code == aegis::reflect::ErrorCode::DuplicateName);
    }
}

TEST_SUITE("world::ComponentStorage")
{
    TEST_CASE("finds a component only on entities that have it")
    {
        TypeRegistry types;
        REQUIRE(aegis::world::registerComponents(types).has_value());
        Registry registry;
        auto lit = registry.create("Lit");
        auto dark = registry.create("Dark");
        registry.add<aegis::PointLight>(lit).intensity = 5.0f;

        const auto& storage = storageOf(types, "PointLight");
        auto* light = static_cast<aegis::PointLight*>(storage.find(registry, lit));
        REQUIRE(light != nullptr);
        CHECK(light->intensity == 5.0f);
        CHECK(storage.find(registry, dark) == nullptr);
    }

    TEST_CASE("adds a component by name and sets its fields")
    {
        TypeRegistry types;
        REQUIRE(aegis::world::registerComponents(types).has_value());
        Registry registry;
        auto entity = registry.create();

        const auto* info = types.find("PointLight");
        void* light = storageOf(types, "PointLight").emplace(registry, entity);
        REQUIRE(info->set(light, "intensity", 7.0f).has_value());
        REQUIRE(registry.has<aegis::PointLight>(entity));
        CHECK(registry.get<aegis::PointLight>(entity).intensity == 7.0f);
        CHECK(storageOf(types, "PointLight").emplace(registry, entity) == light); // Already there
    }

    TEST_CASE("reaches the Transform every entity has")
    {
        TypeRegistry types;
        REQUIRE(aegis::world::registerComponents(types).has_value());
        Registry registry;
        auto entity = registry.create("Moved", { 1.0f, 2.0f, 3.0f });
        auto* transform = static_cast<aegis::Transform*>(storageOf(types, "Transform").emplace(registry, entity));
        CHECK(transform == &registry.get<aegis::Transform>(entity));
        CHECK(transform->location.z == 3.0f);
    }

    TEST_CASE("adds and finds a tag")
    {
        TypeRegistry types;
        REQUIRE(aegis::world::registerComponents(types).has_value());
        Registry registry;
        auto entity = registry.create();
        const auto& storage = storageOf(types, "DynamicTag");

        CHECK(storage.find(registry, entity) == nullptr);
        CHECK(storage.emplace(registry, entity) != nullptr);
        CHECK(registry.has<aegis::DynamicTag>(entity));
        CHECK(storage.find(registry, entity) != nullptr);
        CHECK(storage.emplace(registry, entity) != nullptr); // Already there
    }

    TEST_CASE("serves a game's component registered the same way")
    {
        TypeRegistry types;
        auto builder = aegis::world::registerComponent<Health>(types, "Health");
        REQUIRE(builder.has_value());
        REQUIRE(builder->field("value", &Health::value).status().has_value());
        Registry registry;
        auto entity = registry.create();

        void* health = storageOf(types, "Health").emplace(registry, entity);
        REQUIRE(types.find("Health")->set(health, "value", 42.0f).has_value());
        CHECK(registry.get<Health>(entity).value == 42.0f);
    }
}
