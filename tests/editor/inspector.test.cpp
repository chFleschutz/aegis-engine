#include <doctest/doctest.h>

import std;
import aegis.editor.core;
import aegis.math;
import aegis.reflect;
import aegis.world;
import aegis.world.render_components;
import aegis.world.types;

namespace
{
using aegis::editor::addableComponents;
using aegis::editor::addComponent;
using aegis::editor::componentSections;
using aegis::editor::creatableComponents;
using aegis::editor::filterTypes;
using aegis::editor::removeComponent;
using aegis::editor::scriptTypes;
using aegis::editor::setField;
using aegis::reflect::TypeInfo;
using aegis::reflect::TypeRegistry;
using aegis::world::World;

// A game's own component and script: registered like the built-in ones, with no editor code.
struct Health
{
    float value{ 100.0f };
};

struct NoDefault
{
    explicit NoDefault(int value) :
        value{ value }
    {
    }
    int value;
};

struct Patrol
{
    float speed{ 1.0f };
};

auto registerGame(TypeRegistry& types) -> void
{
    REQUIRE(aegis::world::registerComponents(types).has_value());
    auto health = aegis::world::registerComponent<Health>(types, "Health");
    REQUIRE(health.has_value());
    health->category("Gameplay").field("value", &Health::value);
    auto patrol = types.script<Patrol>("Patrol");
    REQUIRE(patrol.has_value());
    patrol->field("speed", &Patrol::speed);
}

auto names(std::span<const TypeInfo* const> types) -> std::vector<std::string>
{
    std::vector<std::string> result;
    for (const auto* type : types)
        result.push_back(type->name);
    return result;
}

auto contains(const std::vector<std::string>& names, std::string_view name) -> bool
{
    return std::ranges::find(names, name) != names.end();
}

auto sectionNames(const TypeRegistry& types, World& world, aegis::world::Entity entity) -> std::vector<std::string>
{
    std::vector<std::string> result;
    for (const auto& section : componentSections(types, world.registry(), entity))
        result.push_back(section.type->name);
    return result;
}
}

TEST_SUITE("editor::componentSections")
{
    TEST_CASE("lists the registered components an entity has, Transform first")
    {
        TypeRegistry types;
        registerGame(types);
        World world;
        auto& registry = world.registry();
        const auto entity = registry.create();
        registry.add<aegis::PointLight>(entity);
        registry.add<Health>(entity);

        CHECK(sectionNames(types, world, entity) == std::vector<std::string>{ "Transform", "PointLight", "Health" });
    }

    TEST_CASE("points each section at the entity's component")
    {
        TypeRegistry types;
        registerGame(types);
        World world;
        auto& registry = world.registry();
        const auto entity = registry.create();
        auto& health = registry.add<Health>(entity);

        const auto sections = componentSections(types, registry, entity);
        const auto it = std::ranges::find_if(sections, [](const auto& section) { return section.type->name == "Health"; });
        REQUIRE(it != sections.end());
        CHECK(it->object == &health);
    }

    TEST_CASE("leaves out unregistered components")
    {
        TypeRegistry types;
        registerGame(types);
        World world;
        const auto entity = world.registry().create("Named");

        // Name, StableId, GlobalTransform and the hierarchy links are the entity's own, not registered
        CHECK(sectionNames(types, world, entity) == std::vector<std::string>{ "Transform" });
    }
}

TEST_SUITE("editor::addableComponents")
{
    TEST_CASE("offers a game's component with no editor code")
    {
        TypeRegistry types;
        registerGame(types);
        World world;
        const auto entity = world.registry().create();

        const auto offered = names(addableComponents(types, world.registry(), entity));
        CHECK(contains(offered, "Health"));
        CHECK(contains(offered, "PointLight"));
        CHECK_FALSE(contains(offered, "Patrol")); // A script: the Add Script menu's
    }

    TEST_CASE("leaves out required components and those the entity has")
    {
        TypeRegistry types;
        registerGame(types);
        World world;
        auto& registry = world.registry();
        const auto entity = registry.create();
        registry.add<Health>(entity);

        const auto offered = names(addableComponents(types, registry, entity));
        CHECK_FALSE(contains(offered, "Transform"));
        CHECK_FALSE(contains(offered, "Health"));
    }

    TEST_CASE("leaves out a singleton another entity has")
    {
        TypeRegistry types;
        registerGame(types);
        World world;
        auto& registry = world.registry();
        const auto entity = registry.create();
        CHECK(contains(names(addableComponents(types, registry, entity)), "Sky"));

        registry.add<aegis::Sky>(registry.create());
        CHECK_FALSE(contains(names(addableComponents(types, registry, entity)), "Sky"));
    }

    TEST_CASE("leaves out a component that cannot be created")
    {
        TypeRegistry types;
        registerGame(types);
        REQUIRE(aegis::world::registerComponent<NoDefault>(types, "NoDefault").has_value());
        World world;
        const auto entity = world.registry().create();

        CHECK_FALSE(contains(names(addableComponents(types, world.registry(), entity)), "NoDefault"));
    }

    TEST_CASE("creatableComponents offers the components for a new entity, without the required ones and a taken singleton")
    {
        TypeRegistry types;
        registerGame(types);
        World world;
        auto& registry = world.registry();

        auto offered = names(creatableComponents(types, registry));
        CHECK(contains(offered, "Health"));
        CHECK(contains(offered, "PointLight"));
        CHECK(contains(offered, "Sky"));
        CHECK_FALSE(contains(offered, "Transform"));
        CHECK_FALSE(contains(offered, "Patrol"));

        registry.add<aegis::Sky>(registry.create());
        offered = names(creatableComponents(types, registry));
        CHECK_FALSE(contains(offered, "Sky"));
        CHECK(contains(offered, "Health"));
    }

    TEST_CASE("sorts by category, then by name")
    {
        TypeRegistry types;
        registerGame(types);
        World world;
        const auto entity = world.registry().create();

        const auto offered = addableComponents(types, world.registry(), entity);
        CHECK(std::ranges::is_sorted(offered, [](const TypeInfo* a, const TypeInfo* b) {
            return std::tuple{ std::string_view{ a->meta.category }, a->displayName() } < std::tuple{ std::string_view{ b->meta.category }, b->displayName() };
        }));
    }
}

TEST_SUITE("editor::scriptTypes")
{
    TEST_CASE("lists the registered scripts only")
    {
        TypeRegistry types;
        registerGame(types);
        CHECK(names(scriptTypes(types)) == std::vector<std::string>{ "Patrol" });
    }
}

TEST_SUITE("editor::filterTypes")
{
    TEST_CASE("keeps every type for an empty filter")
    {
        TypeRegistry types;
        registerGame(types);
        World world;
        const auto entity = world.registry().create();
        const auto addable = addableComponents(types, world.registry(), entity);
        CHECK(names(filterTypes(addable, "")) == names(addable));
    }

    TEST_CASE("matches the display name, the registered name and the category, ignoring case")
    {
        TypeRegistry types;
        registerGame(types);
        World world;
        const auto entity = world.registry().create();
        const auto addable = addableComponents(types, world.registry(), entity);

        CHECK(names(filterTypes(addable, "hEAl")) == std::vector<std::string>{ "Health" });
        CHECK(names(filterTypes(addable, "gameplay")) == std::vector<std::string>{ "Health" });
        CHECK(filterTypes(addable, "no such type").empty());
        // Scripts are filtered the same way
        CHECK(names(filterTypes(scriptTypes(types), "PATR")) == std::vector<std::string>{ "Patrol" });
    }
}

TEST_SUITE("editor::addComponent")
{
    TEST_CASE("gives the entity a default instance")
    {
        TypeRegistry types;
        registerGame(types);
        World world;
        auto& registry = world.registry();
        const auto entity = registry.create();

        REQUIRE(addComponent(*types.find<Health>(), registry, entity).has_value());
        REQUIRE(registry.has<Health>(entity));
        CHECK(registry.get<Health>(entity).value == 100.0f);
    }

    TEST_CASE("adds a tag")
    {
        TypeRegistry types;
        registerGame(types);
        World world;
        auto& registry = world.registry();
        const auto entity = registry.create();

        REQUIRE(addComponent(*types.find<aegis::DynamicTag>(), registry, entity).has_value());
        CHECK(registry.has<aegis::DynamicTag>(entity));
    }

    TEST_CASE("fails for a script")
    {
        TypeRegistry types;
        registerGame(types);
        World world;
        const auto entity = world.registry().create();

        const auto added = addComponent(*types.find<Patrol>(), world.registry(), entity);
        REQUIRE_FALSE(added.has_value());
        CHECK(added.error().code == aegis::reflect::ErrorCode::WrongCategory);
    }

    TEST_CASE("fails for a component that cannot be created")
    {
        TypeRegistry types;
        registerGame(types);
        REQUIRE(aegis::world::registerComponent<NoDefault>(types, "NoDefault").has_value());
        World world;
        const auto entity = world.registry().create();

        const auto added = addComponent(*types.find<NoDefault>(), world.registry(), entity);
        REQUIRE_FALSE(added.has_value());
        CHECK(added.error().code == aegis::reflect::ErrorCode::NotConstructible);
    }
}

TEST_SUITE("editor::removeComponent")
{
    TEST_CASE("removes an optional component")
    {
        TypeRegistry types;
        registerGame(types);
        World world;
        auto& registry = world.registry();
        const auto entity = registry.create();
        registry.add<Health>(entity);

        CHECK(removeComponent(*types.find<Health>(), registry, entity));
        CHECK_FALSE(registry.has<Health>(entity));
        CHECK_FALSE(removeComponent(*types.find<Health>(), registry, entity));
    }

    TEST_CASE("keeps a required component")
    {
        TypeRegistry types;
        registerGame(types);
        World world;
        auto& registry = world.registry();
        const auto entity = registry.create();

        CHECK_FALSE(removeComponent(*types.find<aegis::Transform>(), registry, entity));
        CHECK(registry.has<aegis::Transform>(entity));
    }
}

TEST_SUITE("editor::setField")
{
    TEST_CASE("writes the value through the field")
    {
        TypeRegistry types;
        registerGame(types);
        World world;
        auto& registry = world.registry();
        const auto entity = registry.create();
        auto& health = registry.add<Health>(entity);
        const auto& type = *types.find<Health>();

        REQUIRE(setField(type, *type.findField("value"), registry, entity, &health, aegis::reflect::Value{ 42.0f }).has_value());
        CHECK(health.value == 42.0f);
    }

    TEST_CASE("marks a static entity's moved Transform static-dirty")
    {
        TypeRegistry types;
        registerGame(types);
        World world;
        auto& registry = world.registry();
        const auto entity = registry.create();
        REQUIRE_FALSE(registry.has<aegis::StaticDirtyTag>(entity));
        const auto& type = *types.find<aegis::Transform>();

        auto& transform = registry.get<aegis::Transform>(entity);
        REQUIRE(setField(type, *type.findField("location"), registry, entity, &transform, aegis::reflect::Value{ glm::vec3{ 1.0f, 2.0f, 3.0f } }).has_value());
        CHECK(transform.location.x == 1.0f);
        CHECK(transform.location.z == 3.0f);
        CHECK(registry.has<aegis::StaticDirtyTag>(entity));
    }

    TEST_CASE("prepares the written component before its change hook")
    {
        TypeRegistry types;
        registerGame(types);
        World world;
        auto& registry = world.registry();
        const auto entity = registry.create();
        const auto& type = *types.find<aegis::Transform>();
        auto& transform = registry.get<aegis::Transform>(entity);

        bool dirtyWhenPrepared = true;
        const auto prepare = [&](const TypeInfo& prepared, void* object) {
            CHECK(&prepared == &type);
            CHECK(object == &transform);
            dirtyWhenPrepared = registry.has<aegis::StaticDirtyTag>(entity);
        };
        REQUIRE(setField(type, *type.findField("scale"), registry, entity, &transform, aegis::reflect::Value{ glm::vec3{ 2.0f } }, prepare).has_value());
        CHECK_FALSE(dirtyWhenPrepared);
        CHECK(registry.has<aegis::StaticDirtyTag>(entity));
    }

    TEST_CASE("writes nothing for a value of the wrong kind")
    {
        TypeRegistry types;
        registerGame(types);
        World world;
        auto& registry = world.registry();
        const auto entity = registry.create();
        auto& health = registry.add<Health>(entity);
        const auto& type = *types.find<Health>();

        const auto written = setField(type, *type.findField("value"), registry, entity, &health, aegis::reflect::Value{ std::string{ "full" } });
        REQUIRE_FALSE(written.has_value());
        CHECK(written.error().code == aegis::reflect::ErrorCode::TypeMismatch);
        CHECK(health.value == 100.0f);
    }

    TEST_CASE("writes a script's field, which has no change hook")
    {
        TypeRegistry types;
        registerGame(types);
        World world;
        auto& registry = world.registry();
        const auto entity = registry.create();
        Patrol patrol;
        const auto& type = *types.find<Patrol>();

        REQUIRE(setField(type, *type.findField("speed"), registry, entity, &patrol, aegis::reflect::Value{ 3.0f }).has_value());
        CHECK(patrol.speed == 3.0f);
    }
}
