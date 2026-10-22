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
using aegis::world::Entity;
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

    TEST_CASE("gives the built-in types and their fields editor metadata")
    {
        using aegis::reflect::FieldWidget;
        TypeRegistry types;
        REQUIRE(aegis::world::registerComponents(types).has_value());

        const auto* transform = types.find<aegis::Transform>();
        CHECK(transform->meta.required);
        const auto* light = types.find<aegis::PointLight>();
        CHECK_FALSE(light->meta.required);
        CHECK(light->displayName() == "Point Light");
        CHECK(light->meta.category == "Lighting");
        CHECK(light->findField("color")->meta.widget == FieldWidget::Color);
        CHECK(light->findField("intensity")->meta.min == 0.0);

        const auto& fov = types.find<aegis::Camera>()->findField("fov")->meta;
        CHECK(fov.widget == FieldWidget::Angle);
        REQUIRE(fov.min.has_value());
        REQUIRE(fov.max.has_value());
        CHECK(*fov.min > 0.0);
        CHECK(*fov.max < std::numbers::pi); // Radians, like the field
        const auto& ambient = types.find<aegis::AmbientLight>()->findField("intensity")->meta;
        CHECK(ambient.widget == FieldWidget::Slider);

        const auto* renderer = types.find<aegis::MeshRenderer>();
        CHECK(renderer->findField("mesh")->assetType == "StaticMesh");
        CHECK(renderer->findField("material")->assetType == "Material");
        CHECK(types.find<aegis::Sky>()->findField("environment")->assetType == "Environment");
    }

    TEST_CASE("marks a game's component required when its ComponentTraits say so")
    {
        TypeRegistry types;
        REQUIRE(aegis::world::registerComponent<Health>(types, "Health").has_value());
        CHECK_FALSE(types.find<Health>()->meta.required);
        CHECK(types.find<Health>()->displayName() == "Health");
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

    TEST_CASE("adds a component built by name")
    {
        TypeRegistry types;
        REQUIRE(aegis::world::registerComponents(types).has_value());
        Registry registry;
        auto entity = registry.create();

        const auto* info = types.find("PointLight");
        auto light = info->create();
        REQUIRE(light.has_value());
        REQUIRE(info->set(light->get(), "intensity", 7.0f).has_value());
        storageOf(types, "PointLight").add(registry, entity, light->get());
        REQUIRE(registry.has<aegis::PointLight>(entity));
        CHECK(registry.get<aegis::PointLight>(entity).intensity == 7.0f);
    }

    TEST_CASE("hands construction observers the complete component")
    {
        TypeRegistry types;
        REQUIRE(aegis::world::registerComponents(types).has_value());
        Registry registry;
        auto entity = registry.create();
        float seen = 0.0f;
        auto observe = [&seen, &registry](entt::registry&, entt::entity added) {
            seen = registry.get<aegis::PointLight>(Entity{ added }).intensity;
        };
        registry.onConstruct<aegis::PointLight>().connect<&decltype(observe)::operator()>(observe);

        auto light = types.find("PointLight")->create();
        REQUIRE(light.has_value());
        static_cast<aegis::PointLight*>(light->get())->intensity = 9.0f;
        storageOf(types, "PointLight").add(registry, entity, light->get());
        CHECK(seen == 9.0f);
    }

    TEST_CASE("replaces a component the entity has")
    {
        TypeRegistry types;
        REQUIRE(aegis::world::registerComponents(types).has_value());
        Registry registry;
        auto entity = registry.create("Moved", { 1.0f, 2.0f, 3.0f });
        auto transform = types.find("Transform")->create();
        REQUIRE(transform.has_value());
        static_cast<aegis::Transform*>(transform->get())->location = { 4.0f, 5.0f, 6.0f };

        const auto& storage = storageOf(types, "Transform");
        CHECK(storage.find(registry, entity) == &registry.get<aegis::Transform>(entity));
        storage.add(registry, entity, transform->get());
        CHECK(registry.get<aegis::Transform>(entity).location.z == 6.0f);
    }

    TEST_CASE("adds and finds a tag")
    {
        TypeRegistry types;
        REQUIRE(aegis::world::registerComponents(types).has_value());
        Registry registry;
        auto entity = registry.create();
        const auto& storage = storageOf(types, "DynamicTag");

        CHECK(storage.find(registry, entity) == nullptr);
        auto tag = types.find("DynamicTag")->create();
        REQUIRE(tag.has_value());
        storage.add(registry, entity, tag->get());
        CHECK(registry.has<aegis::DynamicTag>(entity));
        CHECK(storage.find(registry, entity) != nullptr);
        storage.add(registry, entity, tag->get()); // Already there
    }

    TEST_CASE("removes a component the entity has")
    {
        TypeRegistry types;
        REQUIRE(aegis::world::registerComponents(types).has_value());
        Registry registry;
        auto entity = registry.create();
        registry.add<aegis::PointLight>(entity);
        registry.add<aegis::DynamicTag>(entity);

        CHECK(storageOf(types, "PointLight").remove(registry, entity));
        CHECK_FALSE(registry.has<aegis::PointLight>(entity));
        CHECK(storageOf(types, "DynamicTag").remove(registry, entity));
        CHECK_FALSE(registry.has<aegis::DynamicTag>(entity));
        CHECK_FALSE(storageOf(types, "PointLight").remove(registry, entity)); // Already gone
    }

    TEST_CASE("does not remove a required component")
    {
        TypeRegistry types;
        REQUIRE(aegis::world::registerComponents(types).has_value());
        Registry registry;
        auto entity = registry.create();

        CHECK_FALSE(storageOf(types, "Transform").remove(registry, entity));
        CHECK(registry.has<aegis::Transform>(entity));
    }

    TEST_CASE("a changed Transform marks a static entity static-dirty")
    {
        TypeRegistry types;
        REQUIRE(aegis::world::registerComponents(types).has_value());
        Registry registry;
        auto entity = registry.create();
        const auto& storage = storageOf(types, "Transform");

        const auto before = registry.staticVersion();
        static_cast<aegis::Transform*>(storage.find(registry, entity))->location.x = 3.0f;
        storage.changed(registry, entity);
        CHECK(registry.has<aegis::StaticDirtyTag>(entity));
        CHECK(registry.staticVersion() > before);
    }

    TEST_CASE("a changed Transform leaves a dynamic entity alone")
    {
        TypeRegistry types;
        REQUIRE(aegis::world::registerComponents(types).has_value());
        Registry registry;
        auto entity = registry.create();
        registry.add<aegis::DynamicTag>(entity);

        const auto before = registry.staticVersion();
        storageOf(types, "Transform").changed(registry, entity);
        CHECK_FALSE(registry.has<aegis::StaticDirtyTag>(entity));
        CHECK(registry.staticVersion() == before);
    }

    TEST_CASE("a changed MeshRenderer invalidates the static set")
    {
        TypeRegistry types;
        REQUIRE(aegis::world::registerComponents(types).has_value());
        Registry registry;
        auto entity = registry.create();
        registry.add<aegis::MeshRenderer>(entity);

        const auto before = registry.staticVersion();
        storageOf(types, "MeshRenderer").changed(registry, entity);
        CHECK(registry.staticVersion() > before);
        CHECK_FALSE(registry.has<aegis::StaticDirtyTag>(entity));
    }

    TEST_CASE("a changed component of another type changes nothing")
    {
        TypeRegistry types;
        REQUIRE(aegis::world::registerComponents(types).has_value());
        Registry registry;
        auto entity = registry.create();
        registry.add<aegis::PointLight>(entity);

        const auto before = registry.staticVersion();
        storageOf(types, "PointLight").changed(registry, entity);
        CHECK(registry.staticVersion() == before);
        CHECK_FALSE(registry.has<aegis::StaticDirtyTag>(entity));
    }

    TEST_CASE("serves a game's component registered the same way")
    {
        TypeRegistry types;
        auto builder = aegis::world::registerComponent<Health>(types, "Health");
        REQUIRE(builder.has_value());
        REQUIRE(builder->field("value", &Health::value).status().has_value());
        Registry registry;
        auto entity = registry.create();

        auto health = types.find("Health")->create();
        REQUIRE(health.has_value());
        REQUIRE(types.find("Health")->set(health->get(), "value", 42.0f).has_value());
        storageOf(types, "Health").add(registry, entity, health->get());
        CHECK(registry.get<Health>(entity).value == 42.0f);
    }
}
