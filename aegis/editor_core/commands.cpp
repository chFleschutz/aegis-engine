module aegis.editor.core;
import std;
import aegis.reflect;
import aegis.scripting;
import aegis.serialize;
import aegis.world;

namespace aegis::editor
{
namespace
{
using Result = std::expected<void, Error>;

// 'role' names the entity in the message: "entity", "parent".
auto checkEditable(world::Registry& registry, world::Entity entity, std::string_view role = "entity") -> Result
{
    if (!registry.valid(entity))
        return makeError(ErrorCode::NotEditable, std::format("the {} is null or destroyed", role));
    if (registry.has<serialize::InstanceMember>(entity))
        return makeError(ErrorCode::NotEditable, std::format("the {} is part of a scene instance", role));
    return {};
}

// The holder of an Overridable entity and the entity's local ID in the holder's scene.
struct OverrideTarget
{
    world::Entity holder;
    std::uint64_t localId{ 0 };
};

// The target of the field edits of 'entity': none for a Plain entity, an error for a Locked one.
auto overrideTarget(world::Registry& registry, world::Entity entity) -> std::expected<std::optional<OverrideTarget>, Error>
{
    if (!registry.valid(entity))
        return makeError(ErrorCode::NotEditable, "the entity is null or destroyed");
    if (!registry.has<serialize::InstanceMember>(entity))
        return std::nullopt;
    const auto& member = registry.get<serialize::InstanceMember>(entity);
    const auto holder = registry.find(StableId{ member.holder });
    if (!holder || !registry.has<serialize::SceneInstance>(holder))
        return makeError(ErrorCode::NotEditable, "the entity is part of a scene instance whose holder is gone");
    if (registry.has<serialize::InstanceMember>(holder))
        return makeError(ErrorCode::NotEditable, "the entity is part of a scene instance nested in another; edit the outer instance's scene");
    return OverrideTarget{ .holder = holder, .localId = member.localId };
}

// The override edit of the fields 'newValues' of the entity's component 'type', from 'oldValues', recording all of them as
// overridden; only the fields whose value changes or that have no override yet are kept, and no read-only one.
auto overrideEdit(world::Registry& registry, world::Entity entity, const OverrideTarget& target, const reflect::TypeInfo& type,
    const FieldValues& oldValues, const FieldValues& newValues) -> OverrideEdit
{
    const auto& instance = registry.get<serialize::SceneInstance>(target.holder);
    OverrideEdit edit{ .holder = registry.get<StableId>(target.holder), .localId = target.localId, .entity = registry.get<StableId>(entity), .type = type.name };
    for (const auto& value : newValues)
    {
        const auto old = std::ranges::find(oldValues, value.field, &FieldValue::field);
        const auto* field = type.findField(value.field);
        if (old == oldValues.end() || !field || field->meta.readOnly)
            continue;
        const bool overridden = instance.findOverride(target.localId, type.name, value.field) != nullptr;
        if (sameValue(old->value, value.value) && overridden)
            continue;
        edit.oldValues.push_back(*old);
        edit.newValues.push_back(value);
        if (overridden)
            edit.oldOverridden.push_back(value.field);
        edit.newOverridden.push_back(value.field);
    }
    return edit;
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

auto snapshot(const EditWorld& world, world::Entity root) -> std::expected<serialize::Scene, Error>
{
    auto scene = serialize::saveScene(world.registry, world.types, root, world.registry.get<StableId>(root).value);
    if (!scene)
        return makeError(scene.error(), "cannot capture the entities");
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
auto defaultValues(const reflect::TypeInfo& type) -> std::expected<FieldValues, Error>
{
    auto instance = type.create();
    if (!instance)
        return makeError(instance.error(), std::format("cannot create a default '{}'", type.name));
    return fieldValues(type, instance->get());
}

// The entity's component 'type', or with 'scriptIndex' its script of that number and type; null if it has none.
auto findObject(const EditWorld& world, world::Entity entity, const reflect::TypeInfo& type, std::optional<std::size_t> scriptIndex) -> void*
{
    if (!scriptIndex)
    {
        const auto* storage = type.attachment<world::ComponentStorage>();
        return storage ? storage->find(world.registry, entity) : nullptr;
    }
    if (!world.registry.has<scripting::Scripts>(entity))
        return nullptr;
    const auto& entries = world.registry.get<scripting::Scripts>(entity).entries;
    return *scriptIndex < entries.size() && entries[*scriptIndex].name == type.name ? entries[*scriptIndex].object : nullptr;
}

// Writes 'changes' (fields of the entity's component or script) as one edit that undo reverts whole.
auto changeFields(const EditWorld& world, UndoStack& history, world::Entity entity, const reflect::TypeInfo& type, std::optional<std::size_t> scriptIndex, FieldValues changes) -> Result
{
    const auto target = overrideTarget(world.registry, entity);
    if (!target)
        return std::unexpected{ target.error() };
    if (*target && scriptIndex)
        return makeError(ErrorCode::NotEditable, "the scripts of a scene instance's entity cannot be overridden");
    const void* object = findObject(world, entity, type, scriptIndex);
    if (!object)
        return makeError(ErrorCode::MissingComponent, std::format("the entity has no '{}'", type.name));

    auto oldValues = fieldValues(type, object);
    auto newValues = oldValues;
    for (auto& changed : changes)
    {
        const auto field = std::ranges::find(newValues, changed.field, &FieldValue::field);
        if (field != newValues.end())
            field->value = std::move(changed.value);
    }
    if (*target)
    {
        // Only the fields that change become overrides
        FieldValues changed;
        for (const auto& value : newValues)
        {
            const auto old = std::ranges::find(oldValues, value.field, &FieldValue::field);
            if (old != oldValues.end() && !sameValue(old->value, value.value))
                changed.push_back(value);
        }
        return perform(world, history, overrideEdit(world.registry, entity, **target, type, oldValues, changed));
    }
    return perform(world, history, ComponentEdit{ .entity = world.registry.get<StableId>(entity), .type = type.name, .scriptIndex = scriptIndex, .oldValues = std::move(oldValues), .newValues = std::move(newValues) });
}
}

auto createEntity(const EditWorld& world, UndoStack& history, std::string name, world::Entity parent, std::span<const reflect::TypeInfo* const> components)
    -> std::expected<world::Entity, Error>
{
    auto& registry = world.registry;
    if (parent)
    {
        if (auto editable = checkEditable(registry, parent, "parent"); !editable)
            return std::unexpected{ std::move(editable.error()) };
    }
    for (const auto* type : components)
    {
        if (type->category != reflect::TypeCategory::Component || !type->attachment<world::ComponentStorage>())
            return makeError(ErrorCode::WrongCategory, std::format("'{}' is not a component with storage", type->name));
    }

    const auto entity = registry.create(name);
    for (const auto* type : components)
    {
        if (auto added = addComponent(*type, registry, entity); !added)
        {
            registry.destroy(entity);
            return makeError(added.error(), std::format("cannot add '{}'", type->name));
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
    -> std::expected<world::Entity, Error>
{
    auto& registry = world.registry;
    if (parent)
    {
        if (auto editable = checkEditable(registry, parent, "parent"); !editable)
            return std::unexpected{ std::move(editable.error()) };
    }

    const auto holder = create(parent);
    if (!holder)
        return std::unexpected{ holder.error() };
    if (world.scene != 0 && serialize::holdsScene(registry, world.scene, *holder))
    {
        registry.destroySubtree(*holder);
        return makeError(ErrorCode::InstanceCycle, std::format("the instance holds the scene {:016x} it would be part of", world.scene));
    }
    auto scene = snapshot(world, *holder);
    if (!scene)
    {
        registry.destroySubtree(*holder);
        return std::unexpected{ scene.error() };
    }
    history.record(SubtreeEdit{ .subtree = std::move(*scene), .root = registry.get<StableId>(*holder), .parent = idOf(registry, parent), .index = indexOf(registry, *holder) });
    return *holder;
}

auto destroyEntity(const EditWorld& world, UndoStack& history, world::Entity entity) -> std::expected<void, Error>
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

auto duplicateEntity(const EditWorld& world, UndoStack& history, world::Entity entity) -> std::expected<world::Entity, Error>
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
        return makeError(copy.error(), "cannot copy the entities");

    // Recorded with the copy's IDs, so redo restores the same ones
    auto copyScene = snapshot(world, *copy);
    if (!copyScene)
    {
        registry.destroySubtree(*copy);
        return std::unexpected{ copyScene.error() };
    }
    history.record(SubtreeEdit{ .subtree = std::move(*copyScene), .root = registry.get<StableId>(*copy), .parent = idOf(registry, parent), .index = index });
    return *copy;
}

auto renameEntity(const EditWorld& world, UndoStack& history, world::Entity entity, std::string name, bool continuous)
    -> std::expected<void, Error>
{
    auto& registry = world.registry;
    if (auto editable = checkEditable(registry, entity); !editable)
        return editable;
    auto oldName = registry.has<Name>(entity) ? registry.get<Name>(entity).name : std::string{};
    return perform(world, history, RenameEdit{ .entity = registry.get<StableId>(entity), .oldName = std::move(oldName), .newName = std::move(name) }, continuous);
}

auto moveEntity(const EditWorld& world, UndoStack& history, world::Entity entity, world::Entity parent, std::size_t index)
    -> std::expected<void, Error>
{
    auto& registry = world.registry;
    if (auto editable = checkEditable(registry, entity); !editable)
        return editable;
    if (parent)
    {
        if (auto editable = checkEditable(registry, parent, "parent"); !editable)
            return editable;
        if (registry.isInSubtree(parent, entity))
            return makeError(world::HierarchyError::Cycle, "cannot move the entity into its own subtree");
    }

    const auto oldParent = parentOf(registry, entity);
    const HierarchyPlace oldPlace{ .parent = idOf(registry, oldParent), .index = indexOf(registry, entity), .transform = registry.get<Transform>(entity) };
    HierarchyPlace newPlace{ .parent = idOf(registry, parent), .index = parent ? index : 0, .transform = oldPlace.transform };
    if (parent != oldParent) // A reorder keeps the local Transform as it is
    {
        const auto parentGlobal = parent ? worldTransform(registry, parent) : GlobalTransform{};
        newPlace.transform = world::detail::toLocal(parentGlobal, worldTransform(registry, entity).matrix);
    }
    return perform(world, history, MoveEdit{ .entity = registry.get<StableId>(entity), .oldPlace = oldPlace, .newPlace = newPlace });
}

auto moveEntityBeside(const EditWorld& world, UndoStack& history, world::Entity entity, world::Entity sibling, bool after)
    -> std::expected<void, Error>
{
    auto& registry = world.registry;
    if (!registry.valid(sibling))
        return makeError(ErrorCode::MissingEntity, "the target entity is null or destroyed");
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
    -> std::expected<void, Error>
{
    if (auto editable = checkEditable(world.registry, entity); !editable)
        return editable;
    if (type.category != reflect::TypeCategory::Component || !type.attachment<world::ComponentStorage>())
        return makeError(ErrorCode::WrongCategory, std::format("'{}' is not a component with storage", type.name));
    auto values = defaultValues(type);
    if (!values)
        return std::unexpected{ values.error() };
    return perform(world, history, AddComponentEdit{ .entity = world.registry.get<StableId>(entity), .type = type.name, .values = std::move(*values) });
}

auto removeComponent(const EditWorld& world, UndoStack& history, world::Entity entity, const reflect::TypeInfo& type)
    -> std::expected<void, Error>
{
    if (auto editable = checkEditable(world.registry, entity); !editable)
        return editable;
    const auto* storage = type.attachment<world::ComponentStorage>();
    const void* object = storage ? storage->find(world.registry, entity) : nullptr;
    if (!object)
        return makeError(ErrorCode::MissingComponent, std::format("the entity has no '{}'", type.name));
    if (type.meta.required)
        return makeError(ErrorCode::RequiredComponent, std::format("'{}' is required", type.name));
    return perform(world, history, AddComponentEdit{ .entity = world.registry.get<StableId>(entity), .type = type.name, .values = fieldValues(type, object), .remove = true });
}

auto attachScript(const EditWorld& world, UndoStack& history, world::Entity entity, const reflect::TypeInfo& type)
    -> std::expected<void, Error>
{
    auto& registry = world.registry;
    if (auto editable = checkEditable(registry, entity); !editable)
        return editable;
    if (type.category != reflect::TypeCategory::Script)
        return makeError(ErrorCode::WrongCategory, std::format("'{}' is not a script", type.name));
    auto values = defaultValues(type);
    if (!values)
        return std::unexpected{ values.error() };
    const auto index = registry.has<scripting::Scripts>(entity) ? registry.get<scripting::Scripts>(entity).entries.size() : 0;
    return perform(world, history, AttachScriptEdit{ .entity = registry.get<StableId>(entity), .type = type.name, .index = index, .values = std::move(*values) });
}

auto detachScript(const EditWorld& world, UndoStack& history, world::Entity entity, std::size_t index)
    -> std::expected<void, Error>
{
    auto& registry = world.registry;
    if (auto editable = checkEditable(registry, entity); !editable)
        return editable;
    if (!registry.has<scripting::Scripts>(entity) || index >= registry.get<scripting::Scripts>(entity).entries.size())
        return makeError(ErrorCode::MissingComponent, std::format("the entity has no script number {}", index));
    const auto& entry = registry.get<scripting::Scripts>(entity).entries[index];
    const auto* type = world.types.find(entry.name);
    if (!type || !entry.object)
        return makeError(ErrorCode::UnknownType, std::format("the script '{}' is not registered", entry.name));
    return perform(world, history, AttachScriptEdit{ .entity = registry.get<StableId>(entity), .type = type->name, .index = index, .values = fieldValues(*type, entry.object), .detach = true });
}

auto resetFields(const EditWorld& world, UndoStack& history, world::Entity entity, const reflect::TypeInfo& type, std::optional<std::size_t> scriptIndex)
    -> std::expected<void, Error>
{
    auto defaults = defaultValues(type);
    if (!defaults)
        return std::unexpected{ defaults.error() };
    // As Paste does: a read-only field (a SceneInstance's scene) is not the editor's to write
    std::erase_if(*defaults, [&type](const FieldValue& value) {
        const auto* field = type.findField(value.field);
        return field && field->meta.readOnly;
    });
    return changeFields(world, history, entity, type, scriptIndex, std::move(*defaults));
}

auto pasteFields(const EditWorld& world, UndoStack& history, world::Entity entity, const reflect::TypeInfo& type, std::optional<std::size_t> scriptIndex, std::string_view text)
    -> std::expected<void, Error>
{
    auto pasted = componentFromText(type, text);
    if (!pasted)
        return std::unexpected{ pasted.error() };
    return changeFields(world, history, entity, type, scriptIndex, std::move(*pasted));
}

auto editMaterial(const EditWorld& world, UndoStack& history, std::uint64_t material, std::string_view parameter,
    std::optional<reflect::Node> value, bool continuous) -> std::expected<void, Error>
{
    const auto* document = world.materials ? world.materials->find(material) : nullptr;
    if (!document)
        return makeError(ErrorCode::MissingMaterial, std::format("the material {:016x} is not open in the editor", material));
    const auto* current = document->parameter(parameter);
    return perform(world, history,
        MaterialEdit{
            .material = material,
            .file = std::string{ document->fileName() },
            .parameter = std::string{ parameter },
            .oldValue = current ? std::optional{ *current } : std::nullopt,
            .newValue = std::move(value),
        },
        continuous);
}

auto editField(const EditWorld& world, UndoStack& history, world::Entity entity, const reflect::TypeInfo& type, std::optional<std::size_t> scriptIndex, std::string_view field, const reflect::Value& value, bool continuous)
    -> std::expected<void, Error>
{
    const auto target = overrideTarget(world.registry, entity);
    if (!target)
        return std::unexpected{ target.error() };
    if (*target && scriptIndex)
        return makeError(ErrorCode::NotEditable, "the scripts of a scene instance's entity cannot be overridden");
    const void* object = findObject(world, entity, type, scriptIndex);
    if (!object)
        return makeError(ErrorCode::MissingComponent, std::format("the entity has no '{}'", type.name));
    const auto* info = type.findField(field);
    if (!info)
        return makeError(ErrorCode::UnknownField, std::format("'{}' has no field '{}'", type.name, field));
    if (*target)
    {
        if (info->meta.readOnly || type.type == typeid(serialize::SceneInstance))
            return makeError(ErrorCode::NotEditable, std::format("{}.{} of a scene instance's entity cannot be overridden", type.name, info->name));
        const FieldValues oldValues{ { .field = info->name, .value = info->getter(object) } };
        const FieldValues newValues{ { .field = info->name, .value = value } };
        return perform(world, history, overrideEdit(world.registry, entity, **target, type, oldValues, newValues), continuous);
    }
    return perform(world, history,
        FieldEdit{ .entity = world.registry.get<StableId>(entity), .type = type.name, .field = info->name, .scriptIndex = scriptIndex, .oldValue = info->getter(object), .newValue = value },
        continuous);
}

auto entityAccess(world::Registry& registry, world::Entity entity) -> EntityAccess
{
    const auto target = overrideTarget(registry, entity);
    if (!target)
        return EntityAccess::Locked;
    return *target ? EntityAccess::Overridable : EntityAccess::Plain;
}

auto overriddenFields(world::Registry& registry, world::Entity entity, std::string_view type) -> std::vector<std::string>
{
    std::vector<std::string> fields;
    const auto target = overrideTarget(registry, entity);
    if (!target || !*target)
        return fields;
    for (const auto& entry : registry.get<serialize::SceneInstance>((*target)->holder).overrides)
    {
        if (entry.entity == (*target)->localId && entry.component == type)
            fields.push_back(entry.field);
    }
    return fields;
}

auto canUnpack(world::Registry& registry, world::Entity entity) -> bool
{
    return registry.valid(entity) && registry.has<serialize::SceneInstance>(entity) && !registry.has<serialize::InstanceMember>(entity);
}

auto revertOverrides(const EditWorld& world, UndoStack& history, world::Entity entity, const reflect::TypeInfo& type) -> std::expected<void, Error>
{
    auto& registry = world.registry;
    const auto target = overrideTarget(registry, entity);
    if (!target)
        return std::unexpected{ target.error() };
    if (!*target)
        return makeError(ErrorCode::NotEditable, "the entity is not part of a scene instance");
    const void* object = findObject(world, entity, type, std::nullopt);
    if (!object)
        return makeError(ErrorCode::MissingComponent, std::format("the entity has no '{}'", type.name));
    const auto fields = overriddenFields(registry, entity, type.name);
    if (fields.empty())
        return {};
    const auto& scene = registry.get<serialize::SceneInstance>((*target)->holder).scene;
    if (!scene)
        return makeError(ErrorCode::MissingComponent, std::format("the scene {:016x} of the instance is not loaded", scene.id().value));

    OverrideEdit edit{ .holder = registry.get<StableId>((*target)->holder), .localId = (*target)->localId, .entity = registry.get<StableId>(entity), .type = type.name, .oldOverridden = fields };
    for (const auto& name : fields)
    {
        const auto* field = type.findField(name);
        if (!field)
            return makeError(ErrorCode::UnknownField, std::format("'{}' has no field '{}'", type.name, name));
        auto value = serialize::sceneValue(*scene, (*target)->localId, type, *field);
        if (!value)
            return makeError(value.error(), std::format("cannot read the scene's {}.{}", type.name, name));
        edit.oldValues.push_back({ .field = field->name, .value = field->getter(object) });
        edit.newValues.push_back({ .field = field->name, .value = std::move(*value) });
    }
    return perform(world, history, std::move(edit));
}

auto unpackInstance(const EditWorld& world, UndoStack& history, world::Entity holder) -> std::expected<void, Error>
{
    auto& registry = world.registry;
    if (!registry.valid(holder))
        return makeError(ErrorCode::NotEditable, "the entity is null or destroyed");
    if (!registry.has<serialize::SceneInstance>(holder))
        return makeError(ErrorCode::MissingComponent, "the entity holds no scene instance");
    if (registry.has<serialize::InstanceMember>(holder))
        return makeError(ErrorCode::NotEditable, "the instance is part of another scene instance; unpack that one first");

    const auto holderId = registry.get<StableId>(holder);
    UnpackEdit edit{ .holder = holderId, .instance = registry.get<serialize::SceneInstance>(holder) };
    for (const auto handle : registry.view<serialize::InstanceMember>())
    {
        const world::Entity member{ handle };
        const auto& link = registry.get<serialize::InstanceMember>(member);
        if (link.holder == holderId.value)
            edit.members.push_back({ .entity = registry.get<StableId>(member), .member = link });
    }
    std::ranges::sort(edit.members, {}, [](const UnpackedMember& member) { return member.entity.value; });
    return perform(world, history, std::move(edit));
}

TransformDrag::TransformDrag(const EditWorld& world, std::span<const world::Entity> entities, std::string verb) :
    m_verb{ std::move(verb) }
{
    const auto* type = world.types.find<Transform>();
    if (!type)
        return;
    for (const auto entity : entities)
    {
        if (world.registry.valid(entity) && world.registry.has<Transform>(entity) && entityAccess(world.registry, entity) != EntityAccess::Locked)
            m_starts.push_back({ .entity = world.registry.get<StableId>(entity), .values = fieldValues(*type, &world.registry.get<Transform>(entity)) });
    }
}

auto TransformDrag::update(const EditWorld& world, world::Entity entity, const Transform& transform) -> std::expected<void, Error>
{
    auto& registry = world.registry;
    if (!registry.valid(entity) || !registry.has<Transform>(entity))
        return makeError(ErrorCode::MissingComponent, "the entity is gone or has no Transform");
    if (!std::ranges::contains(m_starts, registry.get<StableId>(entity), &Start::entity))
        return makeError(ErrorCode::MissingEntity, "the entity is not part of the drag");
    const auto* type = world.types.find<Transform>();
    if (!type)
        return makeError(ErrorCode::UnknownType, "Transform is not registered");
    if (auto written = writeComponent(*type, registry, entity, &registry.get<Transform>(entity), fieldValues(*type, &transform), world.prepare); !written)
        return makeError(written.error(), "cannot write the Transform");
    return {};
}

void TransformDrag::end(const EditWorld& world, UndoStack& history) const
{
    const auto* type = world.types.find<Transform>();
    if (!type)
        return;
    std::optional<UndoGroup> group;
    if (m_starts.size() > 1)
        group.emplace(history, std::format("{} {} entities", m_verb, m_starts.size()));
    for (const auto& start : m_starts)
    {
        const auto entity = world.registry.find(start.entity);
        if (!entity || !world.registry.has<Transform>(entity))
            continue;
        const auto target = overrideTarget(world.registry, entity);
        if (!target)
            continue;
        if (*target)
        {
            // Written by the drag already: applying it records the overrides
            const auto newValues = fieldValues(*type, &world.registry.get<Transform>(entity));
            FieldValues changed;
            for (const auto& value : newValues)
            {
                const auto old = std::ranges::find(start.values, value.field, &FieldValue::field);
                if (old != start.values.end() && !sameValue(old->value, value.value))
                    changed.push_back(value);
            }
            if (changed.empty())
                continue;
            auto edit = overrideEdit(world.registry, entity, **target, *type, start.values, changed);
            if (auto applied = applyEdit(world, edit, EditSide::New); !applied)
                continue;
            history.record(std::move(edit));
            continue;
        }
        history.record(ComponentEdit{ .entity = start.entity, .type = type->name, .oldValues = start.values, .newValues = fieldValues(*type, &world.registry.get<Transform>(entity)) });
    }
}
}
