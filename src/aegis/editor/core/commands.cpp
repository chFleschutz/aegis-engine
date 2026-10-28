module aegis.editor.core;
import std;
import aegis.reflect;
import aegis.scripting.script;
import aegis.serialize;
import aegis.world;
import aegis.world.systems.transform_system;

namespace aegis::editor
{
namespace
{
using Result = std::expected<void, std::string>;

auto checkEditable(world::Registry& registry, world::Entity entity) -> Result
{
    if (!registry.valid(entity))
        return std::unexpected{ std::string{ "the entity is null or destroyed" } };
    if (registry.has<serialize::InstanceMember>(entity))
        return std::unexpected{ std::string{ "the entity is part of a scene instance" } };
    return {};
}

auto idOf(world::Registry& registry, world::Entity entity) -> std::optional<StableId>
{
    return entity ? std::optional{ registry.get<StableId>(entity) } : std::nullopt;
}

auto parentOf(world::Registry& registry, world::Entity entity) -> world::Entity
{
    return registry.has<Parent>(entity) ? registry.get<Parent>(entity).entity : world::Entity{};
}

// The entity's index among its parent's children; 0 for a root.
auto indexOf(world::Registry& registry, world::Entity entity) -> std::size_t
{
    const auto parent = parentOf(registry, entity);
    if (!parent)
        return 0;
    std::size_t index = 0;
    for (auto child = registry.get<Children>(parent).first; child && child != entity; child = registry.get<Siblings>(child).next)
        ++index;
    return index;
}

auto snapshot(const EditWorld& world, world::Entity root) -> std::expected<serialize::Scene, std::string>
{
    auto scene = serialize::saveScene(world.registry, world.types, root, world.registry.get<StableId>(root).value);
    if (!scene)
        return std::unexpected{ serialize::describe(scene.error()) };
    return std::move(*scene);
}

// Applies 'edit' and records it once it applied.
auto perform(const EditWorld& world, UndoStack& history, Edit edit, bool continuous = false) -> Result
{
    if (auto applied = applyEdit(world, edit, EditSide::New); !applied)
        return applied;
    history.record(std::move(edit), continuous);
    return {};
}

// The field values of a default instance of 'type'.
auto defaultValues(const reflect::TypeInfo& type) -> std::expected<FieldValues, std::string>
{
    auto instance = type.create();
    if (!instance)
        return std::unexpected{ reflect::describe(instance.error()) };
    return fieldValues(type, instance->get());
}
}

auto createEntity(const EditWorld& world, UndoStack& history, std::string name, world::Entity parent, std::span<const reflect::TypeInfo* const> components)
    -> std::expected<world::Entity, std::string>
{
    auto& registry = world.registry;
    if (parent)
    {
        if (auto editable = checkEditable(registry, parent); !editable)
            return std::unexpected{ std::format("cannot create a child: {}", editable.error()) };
    }
    for (const auto* type : components)
    {
        if (type->category != reflect::TypeCategory::Component || !type->attachment<world::ComponentStorage>())
            return std::unexpected{ std::format("'{}' is not a component with storage", type->name) };
    }

    const auto entity = registry.create(name);
    for (const auto* type : components)
    {
        if (auto added = addComponent(*type, registry, entity); !added)
        {
            registry.destroy(entity);
            return std::unexpected{ reflect::describe(added.error()) };
        }
    }
    if (parent)
        std::ignore = registry.addChild(parent, entity); // Both are valid, and a new entity has no children
    auto scene = snapshot(world, entity);
    if (!scene)
    {
        registry.destroy(entity);
        return std::unexpected{ scene.error() };
    }
    history.record(SubtreeEdit{ .subtree = std::move(*scene), .root = registry.get<StableId>(entity), .parent = idOf(registry, parent), .index = indexOf(registry, entity) });
    return entity;
}

auto instantiateEntity(const EditWorld& world, UndoStack& history, world::Entity parent, const CreateInstance& create)
    -> std::expected<world::Entity, std::string>
{
    auto& registry = world.registry;
    if (parent)
    {
        if (auto editable = checkEditable(registry, parent); !editable)
            return std::unexpected{ std::format("cannot instantiate there: {}", editable.error()) };
    }

    const auto holder = create(parent);
    if (!holder)
        return std::unexpected{ holder.error() };
    auto scene = snapshot(world, *holder);
    if (!scene)
    {
        destroySubtree(registry, *holder);
        return std::unexpected{ scene.error() };
    }
    history.record(SubtreeEdit{ .subtree = std::move(*scene), .root = registry.get<StableId>(*holder), .parent = idOf(registry, parent), .index = indexOf(registry, *holder) });
    return *holder;
}

auto destroyEntity(const EditWorld& world, UndoStack& history, world::Entity entity) -> std::expected<void, std::string>
{
    auto& registry = world.registry;
    if (auto editable = checkEditable(registry, entity); !editable)
        return editable;
    auto scene = snapshot(world, entity);
    if (!scene)
        return std::unexpected{ scene.error() };
    return perform(world, history,
        SubtreeEdit{ .subtree = std::move(*scene), .root = registry.get<StableId>(entity), .parent = idOf(registry, parentOf(registry, entity)), .index = indexOf(registry, entity), .destroy = true });
}

auto duplicateEntity(const EditWorld& world, UndoStack& history, world::Entity entity) -> std::expected<world::Entity, std::string>
{
    auto& registry = world.registry;
    if (auto editable = checkEditable(registry, entity); !editable)
        return std::unexpected{ editable.error() };
    auto scene = snapshot(world, entity);
    if (!scene)
        return std::unexpected{ scene.error() };

    const auto parent = parentOf(registry, entity);
    const auto index = indexOf(registry, entity) + 1;
    auto copy = serialize::instantiate(registry, world.types, *scene, parent, index, serialize::EntityIds::New, world.prepare);
    if (!copy)
        return std::unexpected{ serialize::describe(copy.error()) };

    // Recorded with the copy's IDs, so redo restores the same ones
    auto copyScene = snapshot(world, *copy);
    if (!copyScene)
    {
        destroySubtree(registry, *copy);
        return std::unexpected{ copyScene.error() };
    }
    history.record(SubtreeEdit{ .subtree = std::move(*copyScene), .root = registry.get<StableId>(*copy), .parent = idOf(registry, parent), .index = index });
    return *copy;
}

auto renameEntity(const EditWorld& world, UndoStack& history, world::Entity entity, std::string name, bool continuous)
    -> std::expected<void, std::string>
{
    auto& registry = world.registry;
    if (auto editable = checkEditable(registry, entity); !editable)
        return editable;
    auto oldName = registry.has<Name>(entity) ? registry.get<Name>(entity).name : std::string{};
    return perform(world, history, RenameEdit{ .entity = registry.get<StableId>(entity), .oldName = std::move(oldName), .newName = std::move(name) }, continuous);
}

auto moveEntity(const EditWorld& world, UndoStack& history, world::Entity entity, world::Entity parent, std::size_t index)
    -> std::expected<void, std::string>
{
    auto& registry = world.registry;
    if (auto editable = checkEditable(registry, entity); !editable)
        return editable;
    if (parent)
    {
        if (auto editable = checkEditable(registry, parent); !editable)
            return std::unexpected{ std::format("cannot move it there: {}", editable.error()) };
        if (registry.isInSubtree(parent, entity))
            return std::unexpected{ std::string{ world::describe(world::HierarchyError::Cycle) } };
    }

    const auto oldParent = parentOf(registry, entity);
    const HierarchyPlace oldPlace{ .parent = idOf(registry, oldParent), .index = indexOf(registry, entity), .transform = registry.get<Transform>(entity) };
    HierarchyPlace newPlace{ .parent = idOf(registry, parent), .index = parent ? index : 0, .transform = oldPlace.transform };
    if (parent != oldParent) // A reorder keeps the local Transform as it is
    {
        const auto parentGlobal = parent ? worldTransform(registry, parent) : GlobalTransform{};
        newPlace.transform = world::detail::toLocal(parentGlobal, worldTransform(registry, entity).matrix());
    }
    return perform(world, history, MoveEdit{ .entity = registry.get<StableId>(entity), .oldPlace = oldPlace, .newPlace = newPlace });
}

auto moveEntityBeside(const EditWorld& world, UndoStack& history, world::Entity entity, world::Entity sibling, bool after)
    -> std::expected<void, std::string>
{
    auto& registry = world.registry;
    if (!registry.valid(sibling))
        return std::unexpected{ std::string{ "the target entity is null or destroyed" } };
    if (sibling == entity)
        return {};
    const auto parent = parentOf(registry, sibling);
    auto index = indexOf(registry, sibling) + (after ? 1 : 0);
    // moveEntity counts the index without the entity itself
    if (parent && registry.valid(entity) && parentOf(registry, entity) == parent && indexOf(registry, entity) < index)
        --index;
    return moveEntity(world, history, entity, parent, index);
}

auto addComponent(const EditWorld& world, UndoStack& history, world::Entity entity, const reflect::TypeInfo& type)
    -> std::expected<void, std::string>
{
    if (auto editable = checkEditable(world.registry, entity); !editable)
        return editable;
    if (type.category != reflect::TypeCategory::Component || !type.attachment<world::ComponentStorage>())
        return std::unexpected{ std::format("'{}' is not a component with storage", type.name) };
    auto values = defaultValues(type);
    if (!values)
        return std::unexpected{ values.error() };
    return perform(world, history, AddComponentEdit{ .entity = world.registry.get<StableId>(entity), .type = type.name, .values = std::move(*values) });
}

auto removeComponent(const EditWorld& world, UndoStack& history, world::Entity entity, const reflect::TypeInfo& type)
    -> std::expected<void, std::string>
{
    if (auto editable = checkEditable(world.registry, entity); !editable)
        return editable;
    const auto* storage = type.attachment<world::ComponentStorage>();
    const void* object = storage ? storage->find(world.registry, entity) : nullptr;
    if (!object)
        return std::unexpected{ std::format("the entity has no '{}'", type.name) };
    if (type.meta.required)
        return std::unexpected{ std::format("'{}' is required", type.name) };
    return perform(world, history, AddComponentEdit{ .entity = world.registry.get<StableId>(entity), .type = type.name, .values = fieldValues(type, object), .remove = true });
}

auto attachScript(const EditWorld& world, UndoStack& history, world::Entity entity, const reflect::TypeInfo& type)
    -> std::expected<void, std::string>
{
    auto& registry = world.registry;
    if (auto editable = checkEditable(registry, entity); !editable)
        return editable;
    if (type.category != reflect::TypeCategory::Script)
        return std::unexpected{ std::format("'{}' is not a script", type.name) };
    auto values = defaultValues(type);
    if (!values)
        return std::unexpected{ values.error() };
    const auto index = registry.has<scripting::Scripts>(entity) ? registry.get<scripting::Scripts>(entity).entries.size() : 0;
    return perform(world, history, AttachScriptEdit{ .entity = registry.get<StableId>(entity), .type = type.name, .index = index, .values = std::move(*values) });
}

auto detachScript(const EditWorld& world, UndoStack& history, world::Entity entity, std::size_t index)
    -> std::expected<void, std::string>
{
    auto& registry = world.registry;
    if (auto editable = checkEditable(registry, entity); !editable)
        return editable;
    if (!registry.has<scripting::Scripts>(entity) || index >= registry.get<scripting::Scripts>(entity).entries.size())
        return std::unexpected{ std::format("the entity has no script number {}", index) };
    const auto& entry = registry.get<scripting::Scripts>(entity).entries[index];
    const auto* type = world.types.find(entry.name);
    if (!type || !entry.object)
        return std::unexpected{ std::format("the script '{}' is not registered", entry.name) };
    return perform(world, history, AttachScriptEdit{ .entity = registry.get<StableId>(entity), .type = type->name, .index = index, .values = fieldValues(*type, entry.object), .detach = true });
}
}
