#include <doctest/doctest.h>

import std;
import aegis.assets;
import aegis.editor.core;
import aegis.math;
import aegis.reflect;
import aegis.scripting.script;
import aegis.serialize;
import aegis.world;
import aegis.world.types;

namespace
{
using aegis::Children;
using aegis::Name;
using aegis::Parent;
using aegis::Siblings;
using aegis::StableId;
using aegis::Transform;
using aegis::editor::addComponent;
using aegis::editor::applyEdit;
using aegis::editor::attachScript;
using aegis::editor::createEntity;
using aegis::editor::destroyEntity;
using aegis::editor::detachScript;
using aegis::editor::duplicateEntity;
using aegis::editor::EditSide;
using aegis::editor::EditWorld;
using aegis::editor::instantiateEntity;
using aegis::editor::moveEntity;
using aegis::editor::removeComponent;
using aegis::editor::renameEntity;
using aegis::editor::UndoStack;
using aegis::reflect::TypeRegistry;
using aegis::world::Entity;
using aegis::world::Registry;
using aegis::world::World;

// A game's own component and scripts: structural undo needs no editor code for them.
struct Health
{
    float value{ 100.0f };
};

struct Patrol : aegis::scripting::Script
{
    float speed{ 1.0f };
};

struct Wander : aegis::scripting::Script
{
    float radius{ 5.0f };
};

// A device-less world with the built-in and the game's types registered, and its undo history.
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
        auto wander = aegis::scripting::registerScript<Wander>(types, "Wander");
        REQUIRE(wander.has_value());
        wander->field("radius", &Wander::radius);
    }

    auto edits() -> EditWorld { return EditWorld{ types, registry() }; }
    auto registry() -> Registry& { return world.registry(); }
    auto idOf(Entity entity) -> std::uint64_t { return registry().get<StableId>(entity).value; }
    auto find(std::uint64_t id) -> Entity { return registry().find(StableId{ id }); }

    auto undo() -> bool
    {
        const auto* edit = history.undo();
        return edit && applyEdit(edits(), *edit, EditSide::Old).has_value();
    }

    auto redo() -> bool
    {
        const auto* edit = history.redo();
        return edit && applyEdit(edits(), *edit, EditSide::New).has_value();
    }

    // The StableIds of the entity's children, in sibling order.
    auto childIds(Entity parent) -> std::vector<std::uint64_t>
    {
        std::vector<std::uint64_t> ids;
        if (!registry().has<Children>(parent))
            return ids;
        for (auto child = registry().get<Children>(parent).first; child; child = registry().get<Siblings>(child).next)
            ids.push_back(idOf(child));
        return ids;
    }

    auto parentOf(Entity entity) -> Entity { return registry().has<Parent>(entity) ? registry().get<Parent>(entity).entity : Entity{}; }

    auto scriptNames(Entity entity) -> std::vector<std::string>
    {
        std::vector<std::string> names;
        for (const auto& entry : registry().get<aegis::scripting::Scripts>(entity).entries)
            names.push_back(entry.name);
        return names;
    }

    TypeRegistry types;
    World world;
    UndoStack history;
};

auto near(const glm::vec3& a, const glm::vec3& b) -> bool
{
    return glm::length(a - b) < 1e-4f;
}
}

TEST_SUITE("editor::createEntity")
{
    TEST_CASE("undo removes the entity and redo restores it with its StableId")
    {
        Fixture f;
        const auto parent = f.registry().create("Parent");
        const auto created = createEntity(f.edits(), f.history, "Lamp", parent);
        REQUIRE(created.has_value());
        const auto id = f.idOf(*created);
        CHECK(f.childIds(parent) == std::vector{ id });

        CHECK(f.undo());
        CHECK_FALSE(f.find(id));
        CHECK(f.childIds(parent).empty());

        CHECK(f.redo());
        const auto restored = f.find(id);
        REQUIRE(restored);
        CHECK(f.registry().get<Name>(restored).name == "Lamp");
        CHECK(f.childIds(parent) == std::vector{ id });
    }

    TEST_CASE("an entity created with components has them, and one undo removes it with them")
    {
        Fixture f;
        const auto* light = f.types.find("PointLight");
        const auto* camera = f.types.find("Camera");
        REQUIRE(light);
        REQUIRE(camera);
        const std::array components{ light, camera };
        const auto before = f.registry().entityCount();

        const auto created = createEntity(f.edits(), f.history, "Lamp", {}, components);
        REQUIRE(created.has_value());
        CHECK(f.registry().has<aegis::PointLight>(*created));
        CHECK(f.registry().has<aegis::Camera>(*created));
        CHECK(f.registry().get<aegis::PointLight>(*created).intensity == aegis::PointLight{}.intensity);
        const auto id = f.idOf(*created);

        CHECK(f.undo());
        CHECK_FALSE(f.find(id));
        CHECK(f.registry().entityCount() == before);
        CHECK_FALSE(f.history.canUndo()); // Creating it was the only edit

        CHECK(f.redo());
        const auto restored = f.find(id);
        REQUIRE(restored);
        CHECK(f.registry().has<aegis::PointLight>(restored));
        CHECK(f.registry().has<aegis::Camera>(restored));
        CHECK(f.registry().get<aegis::PointLight>(restored).intensity == aegis::PointLight{}.intensity);
    }

    TEST_CASE("a component of a new child is kept by undo and redo under its parent")
    {
        Fixture f;
        const auto parent = f.registry().create("Parent");
        const std::array components{ f.types.find("DirectionalLight") };
        const auto created = createEntity(f.edits(), f.history, "Sun", parent, components);
        REQUIRE(created.has_value());
        const auto id = f.idOf(*created);

        CHECK(f.undo());
        CHECK(f.childIds(parent).empty());
        CHECK(f.redo());
        CHECK(f.childIds(parent) == std::vector{ id });
        CHECK(f.registry().has<aegis::DirectionalLight>(f.find(id)));
    }

    TEST_CASE("a script is not a component to create an entity with")
    {
        Fixture f;
        const std::array components{ f.types.find("Patrol") };
        const auto before = f.registry().entityCount();
        CHECK_FALSE(createEntity(f.edits(), f.history, "Guard", {}, components).has_value());
        CHECK(f.registry().entityCount() == before);
        CHECK_FALSE(f.history.canUndo());
    }
}

TEST_SUITE("editor::instantiateEntity")
{
    // A scene of a lamp with a bulb, as the asset library would have loaded it, and the instances of it it makes.
    struct InstanceFixture : Fixture
    {
        InstanceFixture()
        {
            REQUIRE(aegis::serialize::registerComponents(types).has_value());
            World source;
            auto& registry = source.registry();
            const auto lamp = registry.create("Lamp");
            REQUIRE(registry.addChild(lamp, registry.create("Bulb")).has_value());
            auto saved = aegis::serialize::saveScene(registry, types, lamp, sceneId);
            REQUIRE(saved.has_value());
            scene = aegis::assets::AssetRef<aegis::serialize::Scene>::ready(aegis::assets::AssetId{ sceneId }, std::make_shared<aegis::serialize::Scene>(std::move(*saved)));
        }

        // Resolves the scene of a SceneInstance read back from a snapshot, as the asset library does.
        auto instanceEdits() -> EditWorld
        {
            return EditWorld{ types, registry(), [this](const aegis::reflect::TypeInfo& type, void* component) {
                                 if (type.type == typeid(aegis::serialize::SceneInstance))
                                     static_cast<aegis::serialize::SceneInstance*>(component)->scene = scene;
                             } };
        }

        auto create(Entity parent) -> std::expected<Entity, std::string>
        {
            auto holder = aegis::serialize::instantiate(registry(), types, scene, parent);
            if (!holder)
                return std::unexpected{ aegis::serialize::describe(holder.error()) };
            return *holder;
        }

        static constexpr std::uint64_t sceneId{ 0x5ce4e };
        aegis::assets::AssetRef<aegis::serialize::Scene> scene;
    };

    TEST_CASE("undo removes the instance and redo restores the holder and the scene's entities")
    {
        InstanceFixture f;
        auto& registry = f.registry();
        const auto parent = registry.create("Parent");
        const auto before = registry.entityCount();
        const auto holder = instantiateEntity(f.instanceEdits(), f.history, parent, [&f](Entity at) { return f.create(at); });
        REQUIRE(holder.has_value());
        const auto holderId = f.idOf(*holder);
        const auto memberIds = f.childIds(*holder);
        CHECK(memberIds.size() == 1); // The lamp
        CHECK(f.childIds(parent) == std::vector{ holderId });
        CHECK(registry.entityCount() == before + 3);

        const auto* undone = f.history.undo();
        REQUIRE(undone);
        CHECK(applyEdit(f.instanceEdits(), *undone, EditSide::Old).has_value());
        CHECK_FALSE(f.find(holderId));
        CHECK(registry.entityCount() == before);

        const auto* redone = f.history.redo();
        REQUIRE(redone);
        CHECK(applyEdit(f.instanceEdits(), *redone, EditSide::New).has_value());
        const auto restored = f.find(holderId);
        REQUIRE(restored);
        CHECK(f.childIds(parent) == std::vector{ holderId });
        CHECK(f.childIds(restored) == memberIds); // Derived from the holder's ID, so the same
        CHECK(registry.entityCount() == before + 3);
    }

    TEST_CASE("refuses a parent of a scene instance without creating anything")
    {
        InstanceFixture f;
        const auto member = f.registry().create("Member");
        f.registry().add<aegis::serialize::InstanceMember>(member);
        bool created = false;
        const auto holder = instantiateEntity(f.instanceEdits(), f.history, member, [&](Entity at) {
            created = true;
            return f.create(at);
        });
        CHECK_FALSE(holder.has_value());
        CHECK_FALSE(created);
        CHECK_FALSE(f.history.canUndo());
    }

    TEST_CASE("passes on the error of a failed instance and records nothing")
    {
        InstanceFixture f;
        const auto holder = instantiateEntity(f.instanceEdits(), f.history, {}, [](Entity) -> std::expected<Entity, std::string> { return std::unexpected{ std::string{ "no such scene" } }; });
        REQUIRE_FALSE(holder.has_value());
        CHECK(holder.error() == "no such scene");
        CHECK_FALSE(f.history.canUndo());
    }
}

TEST_SUITE("editor::destroyEntity")
{
    TEST_CASE("destroys the subtree and undo restores the same StableIds, hierarchy and values")
    {
        Fixture f;
        auto& registry = f.registry();
        const auto holder = registry.create("Holder");
        const auto before = registry.create("Before");
        const auto lamp = registry.create("Lamp", glm::vec3{ 1.0f, 2.0f, 3.0f });
        const auto after = registry.create("After");
        for (const auto child : { before, lamp, after })
            REQUIRE(registry.addChild(holder, child).has_value());
        const auto bulb = registry.create("Bulb");
        const auto glass = registry.create("Glass");
        REQUIRE(registry.addChild(lamp, bulb).has_value());
        REQUIRE(registry.addChild(lamp, glass).has_value());
        registry.add<Health>(bulb).value = 7.0f;
        REQUIRE(aegis::scripting::attachScript(registry, f.types, lamp, "Patrol").has_value());

        const auto lampId = f.idOf(lamp);
        const auto bulbId = f.idOf(bulb);
        const auto glassId = f.idOf(glass);
        const auto siblings = f.childIds(holder);

        REQUIRE(destroyEntity(f.edits(), f.history, lamp).has_value());
        CHECK_FALSE(f.find(lampId));
        CHECK_FALSE(f.find(bulbId));
        CHECK_FALSE(f.find(glassId));
        CHECK(registry.entityCount() == 3);

        CHECK(f.undo());
        const auto restored = f.find(lampId);
        REQUIRE(restored);
        CHECK(f.childIds(holder) == siblings); // Back between its siblings
        CHECK(f.childIds(restored) == std::vector{ bulbId, glassId });
        CHECK(near(registry.get<Transform>(restored).location, glm::vec3{ 1.0f, 2.0f, 3.0f }));
        CHECK(registry.get<Health>(f.find(bulbId)).value == 7.0f);
        CHECK(f.scriptNames(restored) == std::vector<std::string>{ "Patrol" });

        CHECK(f.redo());
        CHECK_FALSE(f.find(lampId));
        CHECK_FALSE(f.find(bulbId));
        CHECK(registry.entityCount() == 3);
    }

    TEST_CASE("refuses an entity of a scene instance")
    {
        Fixture f;
        const auto member = f.registry().create("Member");
        f.registry().add<aegis::serialize::InstanceMember>(member);
        CHECK_FALSE(destroyEntity(f.edits(), f.history, member).has_value());
        CHECK(f.registry().valid(member));
        CHECK_FALSE(f.history.canUndo());
    }
}

TEST_SUITE("editor::duplicateEntity")
{
    TEST_CASE("copies the subtree with new StableIds right after the original")
    {
        Fixture f;
        auto& registry = f.registry();
        const auto holder = registry.create("Holder");
        const auto lamp = registry.create("Lamp");
        const auto last = registry.create("Last");
        REQUIRE(registry.addChild(holder, lamp).has_value());
        REQUIRE(registry.addChild(holder, last).has_value());
        const auto bulb = registry.create("Bulb");
        REQUIRE(registry.addChild(lamp, bulb).has_value());
        registry.add<Health>(bulb).value = 3.0f;

        const auto copy = duplicateEntity(f.edits(), f.history, lamp);
        REQUIRE(copy.has_value());
        const auto copyId = f.idOf(*copy);
        CHECK(copyId != f.idOf(lamp));
        CHECK(f.childIds(holder) == std::vector{ f.idOf(lamp), copyId, f.idOf(last) });
        const auto copyChildren = f.childIds(*copy);
        REQUIRE(copyChildren.size() == 1);
        CHECK(copyChildren[0] != f.idOf(bulb));
        CHECK(registry.get<Health>(f.find(copyChildren[0])).value == 3.0f);

        CHECK(f.undo());
        CHECK_FALSE(f.find(copyId));
        CHECK_FALSE(f.find(copyChildren[0]));
        CHECK(f.childIds(holder) == std::vector{ f.idOf(lamp), f.idOf(last) });

        CHECK(f.redo());
        CHECK(f.find(copyId));
        CHECK(f.find(copyChildren[0]));
        CHECK(f.childIds(holder) == std::vector{ f.idOf(lamp), copyId, f.idOf(last) });
    }
}

TEST_SUITE("editor::addComponent")
{
    TEST_CASE("undo removes the added component and redo adds it again")
    {
        Fixture f;
        const auto entity = f.registry().create();
        const auto* health = f.types.find("Health");
        REQUIRE(health);

        REQUIRE(addComponent(f.edits(), f.history, entity, *health).has_value());
        CHECK(f.registry().has<Health>(entity));
        CHECK(f.undo());
        CHECK_FALSE(f.registry().has<Health>(entity));
        CHECK(f.redo());
        CHECK(f.registry().has<Health>(entity));
    }
}

TEST_SUITE("editor::removeComponent")
{
    TEST_CASE("undo adds the component back with its field values")
    {
        Fixture f;
        const auto entity = f.registry().create();
        f.registry().add<Health>(entity).value = 42.0f;
        const auto* health = f.types.find("Health");
        REQUIRE(health);

        REQUIRE(removeComponent(f.edits(), f.history, entity, *health).has_value());
        CHECK_FALSE(f.registry().has<Health>(entity));
        CHECK(f.undo());
        REQUIRE(f.registry().has<Health>(entity));
        CHECK(f.registry().get<Health>(entity).value == 42.0f);
        CHECK(f.redo());
        CHECK_FALSE(f.registry().has<Health>(entity));
    }

    TEST_CASE("refuses a required component")
    {
        Fixture f;
        const auto entity = f.registry().create();
        const auto* transform = f.types.find("Transform");
        REQUIRE(transform);
        CHECK_FALSE(removeComponent(f.edits(), f.history, entity, *transform).has_value());
        CHECK(f.registry().has<Transform>(entity));
        CHECK_FALSE(f.history.canUndo());
    }
}

TEST_SUITE("editor::attachScript")
{
    TEST_CASE("undo detaches the script and redo attaches it again")
    {
        Fixture f;
        const auto entity = f.registry().create();
        const auto* patrol = f.types.find("Patrol");
        REQUIRE(patrol);

        REQUIRE(attachScript(f.edits(), f.history, entity, *patrol).has_value());
        CHECK(f.scriptNames(entity) == std::vector<std::string>{ "Patrol" });
        CHECK(f.undo());
        CHECK(f.scriptNames(entity).empty());
        CHECK(f.redo());
        CHECK(f.scriptNames(entity) == std::vector<std::string>{ "Patrol" });
    }
}

TEST_SUITE("editor::detachScript")
{
    TEST_CASE("undo attaches the script back at its index with its field values")
    {
        Fixture f;
        auto& registry = f.registry();
        const auto entity = registry.create();
        REQUIRE(aegis::scripting::attachScript(registry, f.types, entity, "Wander").has_value());
        auto patrol = aegis::scripting::attachScript(registry, f.types, entity, "Patrol");
        REQUIRE(patrol.has_value());
        static_cast<Patrol*>(*patrol)->speed = 9.0f;
        REQUIRE(aegis::scripting::attachScript(registry, f.types, entity, "Wander").has_value());

        REQUIRE(detachScript(f.edits(), f.history, entity, 1).has_value());
        CHECK(f.scriptNames(entity) == std::vector<std::string>{ "Wander", "Wander" });
        CHECK(f.undo());
        CHECK(f.scriptNames(entity) == std::vector<std::string>{ "Wander", "Patrol", "Wander" });
        const auto& entries = registry.get<aegis::scripting::Scripts>(entity).entries;
        CHECK(static_cast<Patrol*>(entries[1].script.get())->speed == 9.0f);
    }
}

TEST_SUITE("editor::renameEntity")
{
    TEST_CASE("a name typed in one go is one entry")
    {
        Fixture f;
        const auto entity = f.registry().create("Lamp");
        REQUIRE(renameEntity(f.edits(), f.history, entity, "L", true).has_value());
        REQUIRE(renameEntity(f.edits(), f.history, entity, "Li", true).has_value());
        REQUIRE(renameEntity(f.edits(), f.history, entity, "Light", false).has_value());
        CHECK(f.registry().get<Name>(entity).name == "Light");

        CHECK(f.undo());
        CHECK(f.registry().get<Name>(entity).name == "Lamp");
        CHECK_FALSE(f.history.canUndo());
        CHECK(f.redo());
        CHECK(f.registry().get<Name>(entity).name == "Light");
    }
}

TEST_SUITE("editor::moveEntity")
{
    TEST_CASE("reparenting keeps the world position and undo restores the parent, index and Transform")
    {
        Fixture f;
        auto& registry = f.registry();
        const auto oldParent = registry.create("Old", glm::vec3{ 10.0f, 0.0f, 0.0f });
        const auto first = registry.create("First");
        const auto entity = registry.create("Moved", glm::vec3{ 1.0f, 0.0f, 0.0f });
        const auto last = registry.create("Last");
        for (const auto child : { first, entity, last })
            REQUIRE(registry.addChild(oldParent, child).has_value());
        const auto newParent = registry.create("New", glm::vec3{ 0.0f, 5.0f, 0.0f }, glm::quat{ 1.0f, 0.0f, 0.0f, 0.0f }, glm::vec3{ 2.0f });
        const auto worldBefore = aegis::editor::worldTransform(registry, entity).location;

        REQUIRE(moveEntity(f.edits(), f.history, entity, newParent, 0).has_value());
        CHECK(f.parentOf(entity) == newParent);
        CHECK(near(aegis::editor::worldTransform(registry, entity).location, worldBefore));
        CHECK(near(registry.get<Transform>(entity).location, glm::vec3{ 5.5f, -2.5f, 0.0f }));

        CHECK(f.undo());
        CHECK(f.parentOf(entity) == oldParent);
        CHECK(f.childIds(oldParent) == std::vector{ f.idOf(first), f.idOf(entity), f.idOf(last) });
        CHECK(near(registry.get<Transform>(entity).location, glm::vec3{ 1.0f, 0.0f, 0.0f }));

        CHECK(f.redo());
        CHECK(f.parentOf(entity) == newParent);
        CHECK(near(aegis::editor::worldTransform(registry, entity).location, worldBefore));
    }

    TEST_CASE("reordering among the siblings keeps the local Transform")
    {
        Fixture f;
        auto& registry = f.registry();
        const auto parent = registry.create("Parent", glm::vec3{ 3.0f, 0.0f, 0.0f });
        const auto a = registry.create("A", glm::vec3{ 1.0f, 0.0f, 0.0f });
        const auto b = registry.create("B");
        const auto c = registry.create("C");
        for (const auto child : { a, b, c })
            REQUIRE(registry.addChild(parent, child).has_value());

        REQUIRE(moveEntity(f.edits(), f.history, a, parent, 2).has_value());
        CHECK(f.childIds(parent) == std::vector{ f.idOf(b), f.idOf(c), f.idOf(a) });
        CHECK(near(registry.get<Transform>(a).location, glm::vec3{ 1.0f, 0.0f, 0.0f }));
        CHECK(f.undo());
        CHECK(f.childIds(parent) == std::vector{ f.idOf(a), f.idOf(b), f.idOf(c) });
    }

    TEST_CASE("moving beside a sibling places the entity before or after it")
    {
        Fixture f;
        auto& registry = f.registry();
        const auto parent = registry.create("Parent");
        const auto a = registry.create("A");
        const auto b = registry.create("B");
        const auto c = registry.create("C");
        for (const auto child : { a, b, c })
            REQUIRE(registry.addChild(parent, child).has_value());
        const auto other = registry.create("Other");

        REQUIRE(aegis::editor::moveEntityBeside(f.edits(), f.history, a, c, false).has_value());
        CHECK(f.childIds(parent) == std::vector{ f.idOf(b), f.idOf(a), f.idOf(c) });
        REQUIRE(aegis::editor::moveEntityBeside(f.edits(), f.history, a, c, true).has_value());
        CHECK(f.childIds(parent) == std::vector{ f.idOf(b), f.idOf(c), f.idOf(a) });
        REQUIRE(aegis::editor::moveEntityBeside(f.edits(), f.history, other, b, true).has_value());
        CHECK(f.childIds(parent) == std::vector{ f.idOf(b), f.idOf(other), f.idOf(c), f.idOf(a) });
    }

    TEST_CASE("making a child a root entity keeps its world position")
    {
        Fixture f;
        auto& registry = f.registry();
        const auto parent = registry.create("Parent", glm::vec3{ 3.0f, 0.0f, 0.0f });
        const auto child = registry.create("Child", glm::vec3{ 1.0f, 0.0f, 0.0f });
        REQUIRE(registry.addChild(parent, child).has_value());

        REQUIRE(moveEntity(f.edits(), f.history, child, Entity{}, 0).has_value());
        CHECK_FALSE(f.parentOf(child));
        CHECK(near(registry.get<Transform>(child).location, glm::vec3{ 4.0f, 0.0f, 0.0f }));
        CHECK(f.undo());
        CHECK(f.parentOf(child) == parent);
        CHECK(near(registry.get<Transform>(child).location, glm::vec3{ 1.0f, 0.0f, 0.0f }));
    }

    TEST_CASE("refuses to move an entity below itself")
    {
        Fixture f;
        auto& registry = f.registry();
        const auto parent = registry.create("Parent");
        const auto child = registry.create("Child");
        REQUIRE(registry.addChild(parent, child).has_value());

        CHECK_FALSE(moveEntity(f.edits(), f.history, parent, child, 0).has_value());
        CHECK_FALSE(moveEntity(f.edits(), f.history, parent, parent, 0).has_value());
        CHECK(f.parentOf(child) == parent);
        CHECK_FALSE(f.history.canUndo());
    }
}

TEST_SUITE("editor::UndoStack")
{
    TEST_CASE("a field edit of a destroyed entity applies again once undo restored it")
    {
        Fixture f;
        auto& registry = f.registry();
        const auto entity = registry.create("Lamp");
        registry.add<Health>(entity).value = 2.0f; // The edit, already applied
        const auto id = f.idOf(entity);
        f.history.record(aegis::editor::FieldEdit{ .entity = StableId{ id }, .type = "Health", .field = "value", .oldValue = aegis::reflect::Value{ 1.0f }, .newValue = aegis::reflect::Value{ 2.0f } });
        REQUIRE(destroyEntity(f.edits(), f.history, entity).has_value());

        CHECK(f.undo()); // The destroy
        CHECK(f.undo()); // The field edit, on the restored entity
        CHECK(registry.get<Health>(f.find(id)).value == 1.0f);
        CHECK(f.redo());
        CHECK(f.redo());
        CHECK_FALSE(f.find(id));
    }
}
