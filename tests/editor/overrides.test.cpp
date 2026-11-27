#include <doctest/doctest.h>

import std;
import aegis.assets;
import aegis.editor.core;
import aegis.math;
import aegis.reflect;
import aegis.serialize;
import aegis.world;

namespace math = aegis::math;

namespace
{
using aegis::Name;
using aegis::Parent;
using aegis::PointLight;
using aegis::StableId;
using aegis::Transform;
using aegis::assets::AssetId;
using aegis::assets::AssetRef;
using aegis::editor::applyEdits;
using aegis::editor::EditSide;
using aegis::editor::EditWorld;
using aegis::editor::EntityAccess;
using aegis::editor::ErrorCode;
using aegis::editor::UndoStack;
using aegis::reflect::TypeRegistry;
using aegis::serialize::deriveEntityId;
using aegis::serialize::InstanceMember;
using aegis::serialize::Scene;
using aegis::serialize::SceneInstance;
using aegis::world::Entity;
using aegis::world::Registry;
using aegis::world::World;

constexpr std::uint64_t levelId = 0x00000000000000aa;
constexpr std::uint64_t propId = 0x00000000000000bb;
constexpr std::uint64_t lampId = 0x00000000000000cc;
constexpr std::uint64_t holderId = 0x0000000000000010;
constexpr std::uint64_t nestedLocalId = 0x0000000000000005; // The prop's holder of a lamp

auto sceneText(std::uint64_t id, std::string_view entities) -> std::string
{
    return std::format(R"({{
  "type": "Scene",
  "id": "{:016x}",
  "version": 1,
  "types": {{ "PointLight": 1, "SceneInstance": 1, "Transform": 1 }},
  "entities": [
{}
  ]
}}
)",
        id, entities);
}

auto lampText(std::uint64_t id, float intensity, std::string_view children = {}) -> std::string
{
    return std::format(R"(    {{ "id": "{:016x}", "name": "Lamp", "children": [{}], "components": {{ "PointLight": {{ "intensity": {} }} }} }},)", id, children, intensity);
}

auto holderText(std::uint64_t id, std::uint64_t scene, std::string_view overrides = {}) -> std::string
{
    return std::format(R"(    {{ "id": "{:016x}", "name": "Holder", "components": {{ "SceneInstance": {{ "scene": "{:016x}" }} }}{}{} }},)", id, scene,
        overrides.empty() ? "" : R"(, "overrides": )", overrides);
}

// A level holding a prop: lamps 1 (with child 2) and a nested instance (5) of a lamp scene with its own lamp 1
struct Fixture
{
    Fixture()
    {
        REQUIRE(aegis::world::registerComponents(types).has_value());
        REQUIRE(aegis::serialize::registerComponents(types).has_value());
        add(lampId, lampText(1, 50.0f));
        add(propId, lampText(1, 100.0f, R"("0000000000000002")") + "\n" + lampText(2, 100.0f) + "\n" + holderText(nestedLocalId, lampId));
        const Scene& level = add(levelId, holderText(holderId, propId, R"({ "0000000000000002": { "PointLight": { "intensity": 300 } } })"));
        REQUIRE(aegis::serialize::instantiate(registry(), types, level, prepare()).has_value());
    }

    auto add(std::uint64_t id, std::string_view entities) -> const Scene&
    {
        auto scene = aegis::serialize::parseScene(sceneText(id, entities), types);
        REQUIRE(scene.has_value());
        auto shared = std::make_shared<Scene>(std::move(*scene));
        refs.insert_or_assign(id, AssetRef<Scene>::ready(AssetId{ id }, shared));
        return *shared;
    }

    auto prepare() -> aegis::serialize::PrepareComponent
    {
        return [this](const aegis::reflect::TypeInfo& type, void* component) {
            if (type.type != typeid(SceneInstance))
                return;
            auto& instance = *static_cast<SceneInstance*>(component);
            if (auto it = refs.find(instance.scene.id().value); it != refs.end())
                instance.scene = it->second;
        };
    }

    auto edits() -> EditWorld { return EditWorld{ .types = types, .registry = registry(), .prepare = prepare(), .scene = levelId }; }
    auto registry() -> Registry& { return world.registry(); }
    auto find(std::uint64_t id) -> Entity
    {
        const Entity entity = registry().find(StableId{ id });
        REQUIRE(entity);
        return entity;
    }
    auto holder() -> Entity { return find(holderId); }
    auto member(std::uint64_t localId) -> Entity { return find(deriveEntityId(holderId, localId)); }
    auto nestedMember() -> Entity { return find(deriveEntityId(deriveEntityId(holderId, nestedLocalId), 1)); }
    auto instance() -> SceneInstance& { return registry().get<SceneInstance>(holder()); }
    auto type(std::string_view name) -> const aegis::reflect::TypeInfo&
    {
        const auto* found = types.find(name);
        REQUIRE(found);
        return *found;
    }

    auto undo() -> bool
    {
        const auto entry = history.undo();
        return !entry.empty() && applyEdits(edits(), entry, EditSide::Old).empty();
    }

    auto redo() -> bool
    {
        const auto entry = history.redo();
        return !entry.empty() && applyEdits(edits(), entry, EditSide::New).empty();
    }

    // What the world looks like: every entity's name, parent and Transform location, and its light's intensity
    auto picture() -> std::map<std::uint64_t, std::tuple<std::string, std::uint64_t, math::Float3, float>>
    {
        std::map<std::uint64_t, std::tuple<std::string, std::uint64_t, math::Float3, float>> result;
        for (const auto handle : registry().view<StableId>())
        {
            const Entity entity{ handle };
            const Entity parent = registry().get<Parent>(entity).entity;
            result.emplace(registry().get<StableId>(entity).value,
                std::tuple{ registry().get<Name>(entity).name, parent ? registry().get<StableId>(parent).value : 0, registry().get<Transform>(entity).location,
                    registry().has<PointLight>(entity) ? registry().get<PointLight>(entity).intensity : -1.0f });
        }
        return result;
    }

    TypeRegistry types;
    World world;
    UndoStack history;
    std::map<std::uint64_t, AssetRef<Scene>> refs;
};

auto intensityOverride(SceneInstance& instance, std::uint64_t localId) -> std::optional<float>
{
    const auto* entry = instance.findOverride(localId, "PointLight", "intensity");
    return entry ? std::optional{ std::get<float>(entry->value) } : std::nullopt;
}
}

TEST_SUITE("editor::entityAccess")
{
    TEST_CASE("tells plain, overridable and locked entities apart")
    {
        Fixture f;
        auto& registry = f.registry();
        CHECK(aegis::editor::entityAccess(registry, f.holder()) == EntityAccess::Plain);
        CHECK(aegis::editor::entityAccess(registry, f.member(1)) == EntityAccess::Overridable);
        CHECK(aegis::editor::entityAccess(registry, f.member(nestedLocalId)) == EntityAccess::Overridable);
        CHECK(aegis::editor::entityAccess(registry, f.nestedMember()) == EntityAccess::Locked);

        CHECK(aegis::editor::overriddenFields(registry, f.member(2), "PointLight") == std::vector<std::string>{ "intensity" });
        CHECK(aegis::editor::overriddenFields(registry, f.member(1), "PointLight").empty());
        CHECK(aegis::editor::canUnpack(registry, f.holder()));
        CHECK_FALSE(aegis::editor::canUnpack(registry, f.member(nestedLocalId)));
        CHECK_FALSE(aegis::editor::canUnpack(registry, f.member(1)));
    }
}

TEST_SUITE("editor::editField overrides")
{
    TEST_CASE("an edit of an instance's entity records an override that undo removes and redo restores")
    {
        Fixture f;
        const auto lamp = f.member(1);
        REQUIRE(aegis::editor::editField(f.edits(), f.history, lamp, f.type("PointLight"), std::nullopt, "intensity", 250.0f).has_value());
        CHECK(f.registry().get<PointLight>(lamp).intensity == 250.0f);
        CHECK(intensityOverride(f.instance(), 1) == 250.0f);
        CHECK(f.history.nextUndoTitle().empty());
        CHECK(aegis::editor::entryTitle(f.history.nextUndo(), {}, f.registry()) == "Edit PointLight.intensity 'Lamp'");

        CHECK(f.undo());
        CHECK(f.registry().get<PointLight>(lamp).intensity == 100.0f);
        CHECK_FALSE(intensityOverride(f.instance(), 1));
        CHECK(f.redo());
        CHECK(f.registry().get<PointLight>(lamp).intensity == 250.0f);
        CHECK(intensityOverride(f.instance(), 1) == 250.0f);
    }

    TEST_CASE("an edit of an overridden field keeps the override on undo")
    {
        Fixture f;
        const auto lamp = f.member(2);
        REQUIRE(aegis::editor::editField(f.edits(), f.history, lamp, f.type("PointLight"), std::nullopt, "intensity", 400.0f).has_value());
        CHECK(intensityOverride(f.instance(), 2) == 400.0f);
        CHECK(f.undo());
        CHECK(f.registry().get<PointLight>(lamp).intensity == 300.0f);
        CHECK(intensityOverride(f.instance(), 2) == 300.0f);
    }

    TEST_CASE("a drag of a field is one entry")
    {
        Fixture f;
        const auto lamp = f.member(1);
        for (const float value : { 110.0f, 120.0f, 130.0f })
            REQUIRE(aegis::editor::editField(f.edits(), f.history, lamp, f.type("PointLight"), std::nullopt, "intensity", value, true).has_value());
        f.history.seal();
        CHECK(intensityOverride(f.instance(), 1) == 130.0f);
        CHECK(f.undo());
        CHECK(f.registry().get<PointLight>(lamp).intensity == 100.0f);
        CHECK_FALSE(intensityOverride(f.instance(), 1));
        CHECK_FALSE(f.history.canUndo());
    }

    TEST_CASE("saving the world writes the override, and a destroyed holder keeps it through undo")
    {
        Fixture f;
        REQUIRE(aegis::editor::editField(f.edits(), f.history, f.member(1), f.type("PointLight"), std::nullopt, "intensity", 250.0f).has_value());
        auto saved = aegis::serialize::saveScene(f.registry(), f.types, levelId);
        REQUIRE(saved.has_value());
        CHECK(aegis::serialize::writeScene(*saved).contains(R"("0000000000000001": {)"));

        REQUIRE(aegis::editor::destroyEntity(f.edits(), f.history, f.holder()).has_value());
        CHECK(f.undo());
        CHECK(f.registry().get<PointLight>(f.member(1)).intensity == 250.0f);
        CHECK(intensityOverride(f.instance(), 1) == 250.0f);
    }

    TEST_CASE("refuses a script, a locked entity and a structural edit")
    {
        Fixture f;
        CHECK(aegis::editor::editField(f.edits(), f.history, f.nestedMember(), f.type("PointLight"), std::nullopt, "intensity", 1.0f).error().code == ErrorCode::NotEditable);
        CHECK(aegis::editor::editField(f.edits(), f.history, f.member(1), f.type("PointLight"), std::size_t{ 0 }, "intensity", 1.0f).error().code == ErrorCode::NotEditable);
        CHECK(aegis::editor::renameEntity(f.edits(), f.history, f.member(1), "Other").error().code == ErrorCode::NotEditable);
        CHECK(aegis::editor::removeComponent(f.edits(), f.history, f.member(1), f.type("PointLight")).error().code == ErrorCode::NotEditable);
        CHECK_FALSE(f.history.canUndo());
    }

    TEST_CASE("a reset overrides only the fields it changes")
    {
        Fixture f;
        const auto lamp = f.member(1);
        auto& registry = f.registry();
        registry.get<PointLight>(lamp).intensity = 100.0f; // The default color and range already
        REQUIRE(aegis::editor::resetFields(f.edits(), f.history, lamp, f.type("PointLight")).has_value());
        CHECK(registry.get<PointLight>(lamp).intensity == PointLight{}.intensity);
        const auto fields = aegis::editor::overriddenFields(registry, lamp, "PointLight");
        CHECK(fields.empty()); // 100 is the default: nothing changed, nothing recorded
        CHECK_FALSE(f.history.canUndo());

        REQUIRE(aegis::editor::editField(f.edits(), f.history, lamp, f.type("PointLight"), std::nullopt, "range", 3.0f).has_value());
        REQUIRE(aegis::editor::resetFields(f.edits(), f.history, lamp, f.type("PointLight")).has_value());
        CHECK(aegis::editor::overriddenFields(registry, lamp, "PointLight") == std::vector<std::string>{ "range" });
        CHECK(registry.get<PointLight>(lamp).range == PointLight{}.range);
    }
}

TEST_SUITE("editor::resetFields overrides")
{
    TEST_CASE("a reset leaves a nested holder's scene alone")
    {
        Fixture f;
        const auto nested = f.member(nestedLocalId);
        REQUIRE(aegis::editor::resetFields(f.edits(), f.history, nested, f.type("SceneInstance")).has_value());
        CHECK(f.registry().get<SceneInstance>(nested).scene.id().value == lampId);
        CHECK(aegis::editor::overriddenFields(f.registry(), nested, "SceneInstance").empty());
        CHECK_FALSE(f.history.canUndo());
        CHECK(aegis::editor::editField(f.edits(), f.history, nested, f.type("SceneInstance"), std::nullopt, "scene", std::uint64_t{ 0 }).error().code == ErrorCode::NotEditable);
    }
}

TEST_SUITE("editor::revertOverrides")
{
    TEST_CASE("writes the scene's values back and drops the overrides, and undo brings them back")
    {
        Fixture f;
        const auto lamp = f.member(2);
        REQUIRE(aegis::editor::editField(f.edits(), f.history, lamp, f.type("PointLight"), std::nullopt, "range", 4.0f).has_value());
        REQUIRE(aegis::editor::revertOverrides(f.edits(), f.history, lamp, f.type("PointLight")).has_value());
        CHECK(f.registry().get<PointLight>(lamp).intensity == 100.0f);
        CHECK(f.registry().get<PointLight>(lamp).range == PointLight{}.range); // The scene leaves it out: the default
        CHECK(aegis::editor::overriddenFields(f.registry(), lamp, "PointLight").empty());
        CHECK(aegis::editor::entryTitle(f.history.nextUndo(), {}, f.registry()) == "Revert PointLight 'Lamp'");

        CHECK(f.undo());
        CHECK(f.registry().get<PointLight>(lamp).intensity == 300.0f);
        CHECK(f.registry().get<PointLight>(lamp).range == 4.0f);
        CHECK(aegis::editor::overriddenFields(f.registry(), lamp, "PointLight") == std::vector<std::string>{ "intensity", "range" });
    }

    TEST_CASE("reverts a Transform the scene file leaves out to the default")
    {
        Fixture f;
        const auto lamp = f.member(1);
        REQUIRE(aegis::editor::editField(f.edits(), f.history, lamp, f.type("Transform"), std::nullopt, "location", math::Float3{ 1.0f, 2.0f, 3.0f }).has_value());
        CHECK(aegis::editor::entryTitle(f.history.nextUndo(), {}, f.registry()) == "Move 'Lamp'");
        REQUIRE(aegis::editor::revertOverrides(f.edits(), f.history, lamp, f.type("Transform")).has_value());
        CHECK(f.registry().get<Transform>(lamp).location == math::Float3{});
    }

    TEST_CASE("records nothing without overrides and refuses an entity outside an instance")
    {
        Fixture f;
        REQUIRE(aegis::editor::revertOverrides(f.edits(), f.history, f.member(1), f.type("PointLight")).has_value());
        CHECK_FALSE(f.history.canUndo());
        CHECK(aegis::editor::revertOverrides(f.edits(), f.history, f.holder(), f.type("Transform")).error().code == ErrorCode::NotEditable);
    }
}

TEST_SUITE("editor::unpackInstance")
{
    TEST_CASE("makes the instance's entities plain and keeps the world as it was, and undo links them again")
    {
        Fixture f;
        auto& registry = f.registry();
        const auto before = f.picture();
        REQUIRE(aegis::editor::unpackInstance(f.edits(), f.history, f.holder()).has_value());
        CHECK(f.picture() == before);
        CHECK_FALSE(registry.has<SceneInstance>(f.holder()));
        CHECK_FALSE(registry.has<InstanceMember>(f.member(1)));
        CHECK_FALSE(registry.has<InstanceMember>(f.member(2)));
        CHECK(registry.get<PointLight>(f.member(2)).intensity == 300.0f);
        // The instance it held stays one
        CHECK(registry.has<SceneInstance>(f.member(nestedLocalId)));
        CHECK(registry.has<InstanceMember>(f.nestedMember()));
        CHECK(aegis::editor::entityAccess(registry, f.member(1)) == EntityAccess::Plain);
        CHECK(aegis::editor::entityAccess(registry, f.nestedMember()) == EntityAccess::Overridable);
        CHECK(aegis::editor::entryTitle(f.history.nextUndo(), {}, registry) == "Unpack 'Holder'");

        // Saved and loaded again: the same world, now without the prop
        auto saved = aegis::serialize::saveScene(registry, f.types, levelId);
        REQUIRE(saved.has_value());
        auto read = aegis::serialize::parseScene(aegis::serialize::writeScene(*saved), f.types);
        REQUIRE(read.has_value());
        World loaded;
        REQUIRE(aegis::serialize::instantiate(loaded.registry(), f.types, *read, f.prepare()).has_value());
        CHECK(loaded.registry().entityCount() == registry.entityCount());

        CHECK(f.undo());
        CHECK(f.picture() == before);
        CHECK(registry.has<SceneInstance>(f.holder()));
        CHECK(intensityOverride(f.instance(), 2) == 300.0f);
        CHECK(registry.get<InstanceMember>(f.member(2)).localId == 2);
        CHECK(aegis::editor::entityAccess(registry, f.nestedMember()) == EntityAccess::Locked);
        CHECK(f.redo());
        CHECK_FALSE(registry.has<SceneInstance>(f.holder()));
        CHECK(f.picture() == before);
    }

    TEST_CASE("refuses an entity that holds no instance and a nested instance")
    {
        Fixture f;
        CHECK(aegis::editor::unpackInstance(f.edits(), f.history, f.member(1)).error().code == ErrorCode::MissingComponent);
        CHECK(aegis::editor::unpackInstance(f.edits(), f.history, f.member(nestedLocalId)).error().code == ErrorCode::NotEditable);
        const auto plain = f.registry().create("Plain");
        CHECK(aegis::editor::unpackInstance(f.edits(), f.history, plain).error().code == ErrorCode::MissingComponent);
        CHECK_FALSE(f.history.canUndo());
    }
}

TEST_SUITE("editor::instantiateEntity cycles")
{
    TEST_CASE("refuses an instance that holds the scene being edited")
    {
        Fixture f;
        // Edited as the lamp scene: the prop holds a lamp
        auto edits = f.edits();
        edits.scene = lampId;
        const auto count = f.registry().entityCount();
        const auto holder = aegis::editor::instantiateEntity(edits, f.history, {}, [&f](Entity parent) -> std::expected<Entity, aegis::editor::Error> {
            auto created = aegis::serialize::instantiate(f.registry(), f.types, f.refs.at(propId), parent, f.prepare());
            if (!created)
                return aegis::editor::makeError(created.error(), "cannot instantiate");
            return *created;
        });
        REQUIRE_FALSE(holder.has_value());
        CHECK(holder.error().code == ErrorCode::InstanceCycle);
        CHECK(f.registry().entityCount() == count);
        CHECK_FALSE(f.history.canUndo());

        // Edited as the level, it is fine
        edits.scene = levelId;
        CHECK(aegis::editor::instantiateEntity(edits, f.history, {}, [&f](Entity parent) -> std::expected<Entity, aegis::editor::Error> {
            return *aegis::serialize::instantiate(f.registry(), f.types, f.refs.at(propId), parent, f.prepare());
        }).has_value());
    }
}

TEST_SUITE("editor::TransformDrag overrides")
{
    TEST_CASE("records a drag of an instance's entity as an override and leaves a locked one out")
    {
        Fixture f;
        auto& registry = f.registry();
        const auto lamp = f.member(1);
        const auto locked = f.nestedMember();
        const std::array entities{ lamp, locked };
        aegis::editor::TransformDrag drag{ f.edits(), entities, "Move" };
        Transform moved;
        moved.location = math::Float3{ 0.0f, 2.0f, 0.0f };
        REQUIRE(drag.update(f.edits(), lamp, moved).has_value());
        CHECK_FALSE(drag.update(f.edits(), locked, moved).has_value());
        drag.end(f.edits(), f.history);

        CHECK(registry.get<Transform>(locked).location == math::Float3{});
        const auto* entry = f.instance().findOverride(1, "Transform", "location");
        REQUIRE(entry);
        CHECK(std::get<math::Float3>(entry->value) == math::Float3{ 0.0f, 2.0f, 0.0f });
        CHECK(aegis::editor::overriddenFields(registry, lamp, "Transform") == std::vector<std::string>{ "location" });

        CHECK(f.undo());
        CHECK(registry.get<Transform>(lamp).location == math::Float3{});
        CHECK(aegis::editor::overriddenFields(registry, lamp, "Transform").empty());
    }
}
