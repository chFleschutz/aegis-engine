module aegis.editor.core;
import std;
import aegis.reflect;
import aegis.scripting.script;
import aegis.serialize;
import aegis.world;
import aegis.world.systems.transform_system;
import aegis.world.types;

namespace aegis::editor
{
namespace
{
using Result = std::expected<void, std::string>;

auto findEntity(world::Registry& registry, StableId id) -> std::expected<world::Entity, std::string>
{
    const auto entity = registry.find(id);
    if (!entity)
        return std::unexpected{ std::format("the entity {:016x} is gone", id.value) };
    return entity;
}

auto findType(const reflect::TypeRegistry& types, std::string_view name) -> std::expected<const reflect::TypeInfo*, std::string>
{
    const auto* type = types.find(name);
    if (!type)
        return std::unexpected{ std::format("no type '{}' is registered", name) };
    return type;
}

// The entity's script number 'index' as its registered type, or null if it has no such script of type 'type'.
auto findScript(world::Registry& registry, world::Entity entity, const reflect::TypeInfo& type, std::size_t index) -> void*
{
    if (!registry.has<scripting::Scripts>(entity))
        return nullptr;
    const auto& entries = registry.get<scripting::Scripts>(entity).entries;
    return index < entries.size() && entries[index].name == type.name ? entries[index].object : nullptr;
}

auto writeValues(const reflect::TypeInfo& type, void* object, const FieldValues& values) -> Result
{
    for (const auto& [name, value] : values)
    {
        const auto* field = type.findField(name);
        if (!field)
            return std::unexpected{ std::format("'{}' has no field '{}'", type.name, name) };
        if (auto written = field->setter(object, value); !written)
            return std::unexpected{ reflect::describe(written.error()) };
    }
    return {};
}

auto applyField(const EditWorld& world, const FieldEdit& edit, EditSide side) -> Result
{
    const auto type = findType(world.types, edit.type);
    if (!type)
        return std::unexpected{ type.error() };
    const auto* field = (*type)->findField(edit.field);
    if (!field)
        return std::unexpected{ std::format("'{}' has no field '{}'", edit.type, edit.field) };
    const auto entity = findEntity(world.registry, edit.entity);
    if (!entity)
        return std::unexpected{ entity.error() };

    void* object = nullptr;
    if (edit.scriptIndex)
        object = findScript(world.registry, *entity, **type, *edit.scriptIndex);
    else if (const auto* storage = (*type)->attachment<world::ComponentStorage>())
        object = storage->find(world.registry, *entity);
    if (!object)
        return std::unexpected{ std::format("the entity no longer has '{}'", edit.type) };

    if (auto written = setField(**type, *field, world.registry, *entity, object, side == EditSide::Old ? edit.oldValue : edit.newValue, world.prepare); !written)
        return std::unexpected{ reflect::describe(written.error()) };
    return {};
}

auto applyComponent(const EditWorld& world, const ComponentEdit& edit, EditSide side) -> Result
{
    const auto type = findType(world.types, edit.type);
    if (!type)
        return std::unexpected{ type.error() };
    const auto entity = findEntity(world.registry, edit.entity);
    if (!entity)
        return std::unexpected{ entity.error() };
    const auto* storage = (*type)->attachment<world::ComponentStorage>();
    void* object = nullptr;
    if (edit.scriptIndex)
        object = findScript(world.registry, *entity, **type, *edit.scriptIndex);
    else if (storage)
        object = storage->find(world.registry, *entity);
    if (!object)
        return std::unexpected{ std::format("the entity no longer has '{}'", edit.type) };

    if (auto written = writeValues(**type, object, side == EditSide::Old ? edit.oldValues : edit.newValues); !written)
        return written;
    if (world.prepare)
        world.prepare(**type, object);
    if (storage && !edit.scriptIndex)
        storage->changed(world.registry, *entity);
    return {};
}

auto applyAddComponent(const EditWorld& world, const AddComponentEdit& edit, EditSide side) -> Result
{
    const auto type = findType(world.types, edit.type);
    if (!type)
        return std::unexpected{ type.error() };
    const auto entity = findEntity(world.registry, edit.entity);
    if (!entity)
        return std::unexpected{ entity.error() };
    const auto* storage = (*type)->attachment<world::ComponentStorage>();
    if (!storage)
        return std::unexpected{ std::format("'{}' is not a component with storage", edit.type) };

    if ((side == EditSide::New) == edit.remove)
    {
        if (!storage->remove(world.registry, *entity))
            return std::unexpected{ std::format("the entity has no '{}' to remove", edit.type) };
        return {};
    }

    if (storage->find(world.registry, *entity))
        return std::unexpected{ std::format("the entity already has '{}'", edit.type) };
    // Built complete before it is added: observers of its construction see the final value
    auto instance = (*type)->create();
    if (!instance)
        return std::unexpected{ reflect::describe(instance.error()) };
    if (auto written = writeValues(**type, instance->get(), edit.values); !written)
        return written;
    if (world.prepare)
        world.prepare(**type, instance->get());
    storage->add(world.registry, *entity, instance->get());
    return {};
}

auto applyAttachScript(const EditWorld& world, const AttachScriptEdit& edit, EditSide side) -> Result
{
    const auto type = findType(world.types, edit.type);
    if (!type)
        return std::unexpected{ type.error() };
    const auto entity = findEntity(world.registry, edit.entity);
    if (!entity)
        return std::unexpected{ entity.error() };

    if ((side == EditSide::New) == edit.detach)
    {
        if (!findScript(world.registry, *entity, **type, edit.index))
            return std::unexpected{ std::format("the entity has no '{}' at {} to detach", edit.type, edit.index) };
        auto& entries = world.registry.get<scripting::Scripts>(*entity).entries;
        entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(edit.index));
        return {};
    }

    if (auto attached = scripting::attachScript(world.registry, world.types, *entity, edit.type); !attached)
        return std::unexpected{ reflect::describe(attached.error()) };
    // Attached last; moved back to its place
    auto& entries = world.registry.get<scripting::Scripts>(*entity).entries;
    const auto index = std::min(edit.index, entries.size() - 1);
    std::ranges::rotate(entries.begin() + static_cast<std::ptrdiff_t>(index), entries.end() - 1, entries.end());
    void* object = entries[index].object;
    if (auto written = writeValues(**type, object, edit.values); !written)
    {
        entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(index));
        return written;
    }
    if (world.prepare)
        world.prepare(**type, object);
    return {};
}

auto applyRename(const EditWorld& world, const RenameEdit& edit, EditSide side) -> Result
{
    const auto entity = findEntity(world.registry, edit.entity);
    if (!entity)
        return std::unexpected{ entity.error() };
    world.registry.getOrAdd<Name>(*entity).name = side == EditSide::Old ? edit.oldName : edit.newName;
    return {};
}

auto applyMove(const EditWorld& world, const MoveEdit& edit, EditSide side) -> Result
{
    const auto entity = findEntity(world.registry, edit.entity);
    if (!entity)
        return std::unexpected{ entity.error() };
    const auto& place = side == EditSide::Old ? edit.oldPlace : edit.newPlace;

    auto& registry = world.registry;
    std::expected<void, world::HierarchyError> moved;
    if (place.parent)
    {
        const auto parent = findEntity(registry, *place.parent);
        if (!parent)
            return std::unexpected{ parent.error() };
        moved = registry.insertChild(*parent, *entity, place.index);
    }
    else
    {
        moved = registry.removeParent(*entity);
    }
    if (!moved)
        return std::unexpected{ std::string{ world::describe(moved.error()) } };

    registry.get<Transform>(*entity) = place.transform;
    if (!registry.has<DynamicTag>(*entity))
        registry.markStaticDirty(*entity);
    return {};
}

auto applySubtree(const EditWorld& world, const SubtreeEdit& edit, EditSide side) -> Result
{
    if ((side == EditSide::New) == edit.destroy)
    {
        const auto root = findEntity(world.registry, edit.root);
        if (!root)
            return std::unexpected{ root.error() };
        destroySubtree(world.registry, *root);
        return {};
    }

    world::Entity parent;
    if (edit.parent)
    {
        const auto found = findEntity(world.registry, *edit.parent);
        if (!found)
            return std::unexpected{ found.error() };
        parent = *found;
    }
    if (auto created = serialize::instantiate(world.registry, world.types, edit.subtree, parent, edit.index, serialize::EntityIds::Keep, world.prepare); !created)
        return std::unexpected{ serialize::describe(created.error()) };
    return {};
}

auto rootName(const SubtreeEdit& edit) -> std::string_view
{
    const auto root = std::ranges::find(edit.subtree.entities, edit.root.value, &serialize::SceneEntity::id);
    return root != edit.subtree.entities.end() ? std::string_view{ root->name } : std::string_view{ "Entity" };
}
}

auto describe(const Edit& edit) -> std::string
{
    return std::visit(
        [](const auto& entry) -> std::string {
            using T = std::decay_t<decltype(entry)>;
            if constexpr (std::same_as<T, FieldEdit>)
                return std::format("the edit of {}.{}", entry.type, entry.field);
            else if constexpr (std::same_as<T, ComponentEdit>)
                return std::format("the edit of {}", entry.type);
            else if constexpr (std::same_as<T, AddComponentEdit>)
                return std::format("{} {}", entry.remove ? "removing" : "adding", entry.type);
            else if constexpr (std::same_as<T, AttachScriptEdit>)
                return std::format("{} {}", entry.detach ? "detaching" : "attaching", entry.type);
            else if constexpr (std::same_as<T, RenameEdit>)
                return std::format("renaming '{}' to '{}'", entry.oldName, entry.newName);
            else if constexpr (std::same_as<T, MoveEdit>)
                return std::format("moving the entity {:016x}", entry.entity.value);
            else
                return std::format("{} '{}'", entry.destroy ? "destroying" : "creating", rootName(entry));
        },
        edit);
}

auto fieldValues(const reflect::TypeInfo& type, const void* object) -> FieldValues
{
    FieldValues values;
    values.reserve(type.fields.size());
    for (const auto& field : type.fields)
        values.push_back({ .field = field.name, .value = field.getter(object) });
    return values;
}

// aegis.math does not export the glm vector operator==, so those compare per component.
auto sameValue(const reflect::Value& a, const reflect::Value& b) -> bool
{
    return a.index() == b.index() && std::visit(
                                         [&b](const auto& left) {
                                             using T = std::decay_t<decltype(left)>;
                                             const auto& right = std::get<T>(b);
                                             if constexpr (std::equality_comparable<T>)
                                             {
                                                 return left == right;
                                             }
                                             else
                                             {
                                                 for (int i = 0; i < T::length(); ++i)
                                                 {
                                                     if (left[i] != right[i])
                                                         return false;
                                                 }
                                                 return true;
                                             }
                                         },
                                         a);
}

auto sameValues(const FieldValues& a, const FieldValues& b) -> bool
{
    return std::ranges::equal(a, b, [](const FieldValue& left, const FieldValue& right) { return left.field == right.field && sameValue(left.value, right.value); });
}

auto worldTransform(world::Registry& registry, world::Entity entity) -> GlobalTransform
{
    std::vector<world::Entity> chain;
    for (auto current = entity; current; current = registry.has<Parent>(current) ? registry.get<Parent>(current).entity : world::Entity{})
        chain.push_back(current);

    GlobalTransform global;
    for (const auto current : chain | std::views::reverse)
        global = world::detail::compose(global, registry.get<Transform>(current));
    return global;
}

void destroySubtree(world::Registry& registry, world::Entity root)
{
    std::vector<world::Entity> entities{ root };
    for (std::size_t i = 0; i < entities.size(); ++i)
    {
        if (!registry.has<Children>(entities[i]))
            continue;
        for (auto child = registry.get<Children>(entities[i]).first; child; child = registry.get<Siblings>(child).next)
            entities.push_back(child);
    }
    for (const auto entity : entities | std::views::reverse)
        registry.destroy(entity);
}

auto applyEdit(const EditWorld& world, const Edit& edit, EditSide side) -> std::expected<void, std::string>
{
    return std::visit(
        [&](const auto& entry) -> Result {
            using T = std::decay_t<decltype(entry)>;
            if constexpr (std::same_as<T, FieldEdit>)
                return applyField(world, entry, side);
            else if constexpr (std::same_as<T, ComponentEdit>)
                return applyComponent(world, entry, side);
            else if constexpr (std::same_as<T, AddComponentEdit>)
                return applyAddComponent(world, entry, side);
            else if constexpr (std::same_as<T, AttachScriptEdit>)
                return applyAttachScript(world, entry, side);
            else if constexpr (std::same_as<T, RenameEdit>)
                return applyRename(world, entry, side);
            else if constexpr (std::same_as<T, MoveEdit>)
                return applyMove(world, entry, side);
            else
                return applySubtree(world, entry, side);
        },
        edit);
}
}
