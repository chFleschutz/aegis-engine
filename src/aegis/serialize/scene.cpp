module;

#include <xxhash.h>

module aegis.serialize;
import std;
import aegis.assets;
import aegis.core;
import aegis.reflect;
import aegis.scripting.script;
import aegis.world.registry;
import aegis.world.types;

namespace aegis::serialize
{
namespace
{
using reflect::Node;
using namespace detail;

constexpr std::string_view sceneType{ "Scene" };

// Brings a saved component or script up to its type's current version and checks every field: that the type
// has it (under its name or an alias), that it is set once, and that its value has the field's kind.
auto readComponent(const reflect::TypeInfo& type, std::uint32_t version, const Node& saved) -> std::expected<SceneComponent, Error>
{
    if (auto checked = expectKind(saved, Node::Kind::Object, std::format("'{}'", type.name)); !checked)
        return std::unexpected{ checked.error() };

    Node fields = saved;
    if (auto upgraded = type.upgrade(fields, version); !upgraded)
    {
        const auto code = upgraded.error().code == reflect::ErrorCode::UnsupportedVersion ? ErrorCode::UnsupportedVersion : ErrorCode::InvalidData;
        return makeError(code, upgraded.error(), std::format("cannot upgrade '{}' from version {}", type.name, version), saved.line);
    }

    SceneComponent component{ .type = &type, .fields = Node::makeObject() };
    component.fields.line = saved.line;
    for (Node& field : fields.items)
    {
        const std::uint32_t line = field.line > 0 ? field.line : saved.line; // A migration's nodes have no line
        const reflect::FieldInfo* info = type.findField(field.key);
        if (!info)
            return makeError(ErrorCode::UnknownField, std::format("type '{}' has no field '{}'", type.name, field.key), line);
        if (component.fields.find(info->name))
            return makeError(ErrorCode::InvalidData, std::format("the field '{}' of '{}' is set twice", info->name, type.name), line);
        if (auto value = reflect::fromNode(info->kind, field, info->enumEntries); !value)
            return makeError(ErrorCode::InvalidValue, value.error(), std::format("bad value for '{}.{}'", type.name, info->name), line);
        field.line = line;
        component.fields.add(info->name, std::move(field));
    }
    return component;
}

// The type a component or script key names, with the version the header gives it.
auto resolveType(const reflect::TypeRegistry& types, const std::map<const reflect::TypeInfo*, std::uint32_t>& versions,
    const Node& entry, reflect::TypeCategory category) -> std::expected<std::pair<const reflect::TypeInfo*, std::uint32_t>, Error>
{
    const reflect::TypeInfo* type = types.find(entry.key);
    if (!type)
        return makeError(ErrorCode::UnknownType, std::format("no type named '{}' is registered", entry.key), entry.line);
    if (type->category != category)
    {
        const bool script = type->category == reflect::TypeCategory::Script;
        return makeError(ErrorCode::InvalidData,
            std::format("'{}' is a {}; list it under \"{}\"", entry.key, script ? "script" : "component", script ? "scripts" : "components"), entry.line);
    }
    auto version = versions.find(type);
    if (version == versions.end())
        return makeError(ErrorCode::InvalidData, std::format("'{}' is missing from the header's \"types\"", entry.key), entry.line);
    return std::pair{ type, version->second };
}

auto readEntity(const Node& node, const reflect::TypeRegistry& types, const std::map<const reflect::TypeInfo*, std::uint32_t>& versions)
    -> std::expected<SceneEntity, Error>
{
    static constexpr auto keys = std::to_array<std::string_view>({ "id", "name", "children", "components", "scripts" });
    if (auto checked = expectKind(node, Node::Kind::Object, "an entity"); !checked)
        return std::unexpected{ checked.error() };
    if (auto checked = checkKeys(node, keys, "an entity"); !checked)
        return std::unexpected{ checked.error() };

    SceneEntity entity{ .line = node.line };
    auto idNode = required(node, "id", Node::Kind::String, "an entity");
    if (!idNode)
        return std::unexpected{ idNode.error() };
    auto id = parseId(**idNode, "an entity's \"id\"");
    if (!id)
        return std::unexpected{ id.error() };
    entity.id = *id;

    auto name = optional(node, "name", Node::Kind::String);
    if (!name)
        return std::unexpected{ name.error() };
    if (*name)
        entity.name = (*name)->text;

    auto children = optional(node, "children", Node::Kind::Array);
    if (!children)
        return std::unexpected{ children.error() };
    if (*children)
    {
        for (const Node& child : (*children)->items)
        {
            auto childId = parseId(child, "a child ID");
            if (!childId)
                return std::unexpected{ childId.error() };
            entity.children.push_back(*childId);
        }
    }

    auto components = optional(node, "components", Node::Kind::Object);
    if (!components)
        return std::unexpected{ components.error() };
    if (*components)
    {
        for (const Node& saved : (*components)->items)
        {
            auto type = resolveType(types, versions, saved, reflect::TypeCategory::Component);
            if (!type)
                return std::unexpected{ type.error() };
            if (std::ranges::contains(entity.components, type->first, &SceneComponent::type))
                return makeError(ErrorCode::InvalidData, std::format("the entity has '{}' twice", type->first->name), saved.line);
            auto component = readComponent(*type->first, type->second, saved);
            if (!component)
                return std::unexpected{ component.error() };
            entity.components.push_back(std::move(*component));
        }
        std::ranges::sort(entity.components, {}, [](const SceneComponent& component) -> const std::string& { return component.type->name; });
    }

    auto scripts = optional(node, "scripts", Node::Kind::Array);
    if (!scripts)
        return std::unexpected{ scripts.error() };
    if (*scripts)
    {
        for (const Node& wrapper : (*scripts)->items)
        {
            if (auto checked = expectKind(wrapper, Node::Kind::Object, "a script"); !checked)
                return std::unexpected{ checked.error() };
            if (wrapper.items.size() != 1)
                return makeError(ErrorCode::InvalidData, "a script is an object with one key, its type", wrapper.line);
            const Node& saved = wrapper.items.front();
            auto type = resolveType(types, versions, saved, reflect::TypeCategory::Script);
            if (!type)
                return std::unexpected{ type.error() };
            auto script = readComponent(*type->first, type->second, saved);
            if (!script)
                return std::unexpected{ script.error() };
            entity.scripts.push_back(std::move(*script));
        }
    }
    return entity;
}

// Children must exist and be listed once in all, and every entity must lead up to a root.
auto checkHierarchy(const std::vector<SceneEntity>& entities, const std::vector<const Node*>& childLists) -> std::expected<void, Error>
{
    std::unordered_map<std::uint64_t, std::size_t> index;
    for (std::size_t i = 0; i < entities.size(); ++i)
        index.emplace(entities[i].id, i);

    std::unordered_map<std::uint64_t, std::uint64_t> parentOf;
    for (std::size_t i = 0; i < entities.size(); ++i)
    {
        const SceneEntity& entity = entities[i];
        for (std::size_t c = 0; c < entity.children.size(); ++c)
        {
            const std::uint64_t child = entity.children[c];
            const std::uint32_t line = childLists[i] ? childLists[i]->items[c].line : entity.line;
            if (!index.contains(child))
                return makeError(ErrorCode::MissingEntity, std::format("the child {} of {} is not in the scene", formatId(child), formatId(entity.id)), line);
            if (auto [it, added] = parentOf.emplace(child, entity.id); !added)
            {
                return makeError(ErrorCode::DuplicateEntity,
                    std::format("{} is listed as a child twice (of {} and of {})", formatId(child), formatId(it->second), formatId(entity.id)), line);
            }
        }
    }

    enum class State
    {
        Visiting,
        Done,
    };
    std::unordered_map<std::uint64_t, State> states;
    for (const SceneEntity& entity : entities)
    {
        std::vector<std::uint64_t> path;
        for (std::uint64_t current = entity.id;;)
        {
            auto state = states.find(current);
            if (state != states.end() && state->second == State::Done)
                break;
            if (state != states.end())
            {
                // `current` is on the path: the path from it on is a cycle
                auto start = std::ranges::find(path, current);
                std::string chain;
                for (auto it = start; it != path.end(); ++it)
                    chain += std::format("{} -> ", formatId(*it));
                chain += formatId(current);
                return makeError(ErrorCode::Cycle, std::format("the entities form a cycle (child -> parent): {}", chain), entities[index.at(current)].line);
            }
            states.emplace(current, State::Visiting);
            path.push_back(current);
            auto parent = parentOf.find(current);
            if (parent == parentOf.end())
                break;
            current = parent->second;
        }
        for (std::uint64_t id : path)
            states[id] = State::Done;
    }
    return {};
}

// The fields of `component` in its type's registration order.
auto orderedFields(const SceneComponent& component) -> Node
{
    Node fields = Node::makeObject();
    for (const reflect::FieldInfo& field : component.type->fields)
    {
        if (const Node* value = component.fields.find(field.name))
            fields.add(field.name, *value);
    }
    return fields;
}
}

namespace detail
{
auto saveFields(const reflect::TypeInfo& type, const void* object) -> std::expected<SceneComponent, Error>
{
    SceneComponent component{ .type = &type, .fields = Node::makeObject() };
    for (const reflect::FieldInfo& field : type.fields)
    {
        auto node = reflect::toNode(field.kind, field.getter(object), field.enumEntries);
        if (!node)
            return makeError(ErrorCode::InvalidValue, node.error(), std::format("cannot save '{}.{}'", type.name, field.name));
        component.fields.add(field.name, std::move(*node));
    }
    return component;
}
}

namespace
{
using StoredComponents = std::vector<std::pair<const reflect::TypeInfo*, const world::ComponentStorage*>>;

// The registered components that live on entities, by name
auto storedComponents(const reflect::TypeRegistry& types) -> StoredComponents
{
    StoredComponents stored;
    for (const reflect::TypeInfo* info : types.types())
    {
        if (info->category != reflect::TypeCategory::Component)
            continue;
        if (const auto* storage = info->attachment<world::ComponentStorage>())
            stored.emplace_back(info, storage);
    }
    std::ranges::sort(stored, {}, [](const auto& entry) -> const std::string& { return entry.first->name; });
    return stored;
}

// The entity with its name, the children that are not instance members, its components and its scripts
auto saveEntity(world::Registry& registry, const reflect::TypeRegistry& types, const StoredComponents& stored, world::Entity entity)
    -> std::expected<SceneEntity, Error>
{
    SceneEntity saved{ .id = registry.get<StableId>(entity).value, .name = registry.get<Name>(entity).name };
    for (world::Entity child = registry.get<Children>(entity).first; child; child = registry.get<Siblings>(child).next)
    {
        if (!registry.has<InstanceMember>(child))
            saved.children.push_back(registry.get<StableId>(child).value);
    }

    for (const auto& [info, storage] : stored)
    {
        if (const void* object = storage->find(registry, entity))
        {
            auto component = saveFields(*info, object);
            if (!component)
                return std::unexpected{ component.error() };
            saved.components.push_back(std::move(*component));
        }
    }

    if (registry.has<scripting::Scripts>(entity))
    {
        for (const scripting::ScriptEntry& entry : registry.get<scripting::Scripts>(entity).entries)
        {
            const reflect::TypeInfo* info = types.find(entry.name);
            if (!info || !entry.object)
                return makeError(ErrorCode::UnknownType, std::format("the script '{}' of entity {} is not registered", entry.name, formatId(saved.id)));
            auto script = saveFields(*info, entry.object);
            if (!script)
                return std::unexpected{ script.error() };
            saved.scripts.push_back(std::move(*script));
        }
    }
    return saved;
}

// The ID of the one entity of `scene` that is no entity's child
auto findRoot(const Scene& scene) -> std::expected<std::uint64_t, Error>
{
    std::unordered_set<std::uint64_t> children;
    for (const SceneEntity& entity : scene.entities)
        children.insert_range(entity.children);

    std::optional<std::uint64_t> root;
    for (const SceneEntity& entity : scene.entities)
    {
        if (children.contains(entity.id))
            continue;
        if (root)
            return makeError(ErrorCode::InvalidData, std::format("the subtree has more than one root ({} and {})", formatId(*root), formatId(entity.id)), entity.line);
        root = entity.id;
    }
    if (!root)
        return makeError(ErrorCode::Cycle, "the subtree has no root");
    return *root;
}

// `scene` with a new random ID for every entity that `registry` does not use, the children lists remapped
auto withNewIds(const Scene& scene, const world::Registry& registry) -> Scene
{
    static thread_local std::mt19937_64 generator{ [] {
        std::random_device device;
        std::seed_seq seed{ device(), device(), device(), device() };
        return std::mt19937_64{ seed };
    }() };

    std::unordered_map<std::uint64_t, std::uint64_t> newIds;
    std::unordered_set<std::uint64_t> taken;
    for (const SceneEntity& entity : scene.entities)
    {
        std::uint64_t id = 0;
        while (id == 0 || taken.contains(id) || registry.find(StableId{ id }))
            id = generator();
        taken.insert(id);
        newIds.emplace(entity.id, id);
    }

    Scene copy = scene;
    for (SceneEntity& entity : copy.entities)
    {
        entity.id = newIds.at(entity.id);
        for (std::uint64_t& child : entity.children)
        {
            if (auto it = newIds.find(child); it != newIds.end())
                child = it->second;
        }
    }
    std::ranges::sort(copy.entities, {}, &SceneEntity::id);
    return copy;
}

auto loadFields(const SceneComponent& component, void* object) -> std::expected<void, Error>
{
    for (const Node& saved : component.fields.items)
    {
        const reflect::FieldInfo* field = component.type->findField(saved.key);
        if (!field)
            return makeError(ErrorCode::UnknownField, std::format("type '{}' has no field '{}'", component.type->name, saved.key), saved.line);
        auto value = reflect::fromNode(field->kind, saved, field->enumEntries);
        if (!value)
            return makeError(ErrorCode::InvalidValue, value.error(), std::format("bad value for '{}.{}'", component.type->name, field->name), saved.line);
        if (auto set = field->setter(object, *value); !set)
            return makeError(ErrorCode::InvalidValue, set.error(), std::format("bad value for '{}.{}'", component.type->name, field->name), saved.line);
    }
    return {};
}

// Creates a scene's entities and then the instances they hold, and destroys all it created when told to.
class Instantiator
{
public:
    Instantiator(world::Registry& registry, const reflect::TypeRegistry& types, const PrepareComponent& prepare) :
        m_registry{ registry },
        m_types{ types },
        m_prepare{ prepare }
    {
    }

    // Creates the entities of `scene`: with their local IDs for a null `holder` (the world's root scene), else
    // with IDs derived from the holder's and as its descendants.
    auto place(const Scene& scene, world::Entity holder) -> std::expected<void, Error>
    {
        if (auto start = std::ranges::find(m_chain, scene.id); start != m_chain.end())
        {
            std::string chain;
            for (auto it = start; it != m_chain.end(); ++it)
                chain += std::format("{} -> ", formatId(*it));
            chain += formatId(scene.id);
            return makeError(ErrorCode::Cycle, std::format("the scene holds itself through its instances: {}", chain));
        }
        m_chain.push_back(scene.id);
        auto placed = placeEntities(scene, holder);
        m_chain.pop_back();
        return placed;
    }

    // Destroys every entity this created.
    void rollback()
    {
        for (auto it = m_created.rbegin(); it != m_created.rend(); ++it)
            m_registry.destroy(*it);
        m_created.clear();
    }

private:
    auto placeEntities(const Scene& scene, world::Entity holder) -> std::expected<void, Error>
    {
        const std::uint64_t holderId = holder ? m_registry.get<StableId>(holder).value : 0;
        std::unordered_map<std::uint64_t, world::Entity> created; // By local ID
        std::vector<std::pair<world::Entity, const SceneEntity*>> holders;

        for (const SceneEntity& saved : scene.entities)
        {
            const std::uint64_t id = holder ? deriveEntityId(holderId, saved.id) : saved.id;
            const world::Entity entity = m_registry.createWithId(id, saved.name);
            if (!entity)
                return makeError(ErrorCode::DuplicateEntity, std::format("the world already has an entity {}", formatId(id)), saved.line);
            m_created.push_back(entity);
            created.emplace(saved.id, entity);
            if (holder)
                m_registry.add<InstanceMember>(entity, InstanceMember{ holderId });

            if (auto added = addComponents(saved, entity, id); !added)
                return std::unexpected{ std::move(added.error()) };
            if (m_registry.has<SceneInstance>(entity))
                holders.emplace_back(entity, &saved);
        }

        for (const SceneEntity& saved : scene.entities)
        {
            const world::Entity parent = created.at(saved.id);
            for (std::uint64_t childId : saved.children)
            {
                auto child = created.find(childId);
                if (child == created.end())
                    return makeError(ErrorCode::MissingEntity, std::format("the child {} is not in the scene", formatId(childId)), saved.line);
                if (m_registry.get<Parent>(child->second).entity)
                    return makeError(ErrorCode::DuplicateEntity, std::format("{} is listed as a child twice", formatId(childId)), saved.line);
                if (!m_registry.addChild(parent, child->second))
                    return makeError(ErrorCode::Cycle, std::format("{} is its own ancestor", formatId(saved.id)), saved.line);
            }
        }

        // The scene's roots become the holder's children, after those its own scene gives it
        if (holder)
        {
            for (const SceneEntity& saved : scene.entities)
            {
                const world::Entity entity = created.at(saved.id);
                if (!m_registry.get<Parent>(entity).entity)
                    std::ignore = m_registry.addChild(holder, entity); // A new root of a new holder: no cycle
            }
        }

        for (const auto& [entity, saved] : holders)
        {
            // A copy: placing the instance adds SceneInstance components, which may move this one
            const assets::AssetRef<Scene> instance = m_registry.get<SceneInstance>(entity).scene;
            if (!instance)
            {
                const auto& slot = instance.slot();
                return makeError(ErrorCode::MissingScene,
                    std::format("the scene {} of entity {} did not load{}{}", formatId(instance.id().value), formatId(saved->id),
                        slot && !slot->message.empty() ? ": " : "", slot ? slot->message : std::string{}),
                    saved->line);
            }
            if (auto placed = place(*instance, entity); !placed)
            {
                if (placed.error().code == ErrorCode::Cycle)
                    return placed;
                return std::unexpected{ Error{ .code = placed.error().code,
                    .message = std::format("in the instance of scene {} on entity {}: {}", formatId(instance->id), formatId(saved->id), describe(placed.error())),
                    .line = saved->line } };
            }
        }
        return {};
    }

    auto addComponents(const SceneEntity& saved, world::Entity entity, std::uint64_t id) -> std::expected<void, Error>
    {
        for (const SceneComponent& component : saved.components)
        {
            const auto* storage = component.type->attachment<world::ComponentStorage>();
            if (!storage)
            {
                return makeError(ErrorCode::UnknownType, std::format("'{}' was not registered with world::registerComponent", component.type->name),
                    component.fields.line);
            }
            if (storage->singleton)
            {
                if (const world::Entity first = storage->first(m_registry))
                {
                    log::warn("The world already has a {} (entity {}): entity {} drops its own", component.type->name,
                        formatId(m_registry.get<StableId>(first).value), formatId(id));
                    continue;
                }
            }
            auto value = component.type->create();
            if (!value)
                return makeError(ErrorCode::UnknownType, value.error(), std::format("cannot create '{}'", component.type->name), component.fields.line);
            if (auto loaded = loadFields(component, value->get()); !loaded)
                return loaded;
            if (m_prepare)
                m_prepare(*component.type, value->get());
            storage->add(m_registry, entity, value->get());
        }

        for (const SceneComponent& script : saved.scripts)
        {
            auto attached = scripting::attachScript(m_registry, m_types, entity, script.type->name);
            if (!attached)
                return makeError(ErrorCode::UnknownType, attached.error(), std::format("cannot attach the script '{}'", script.type->name), script.fields.line);
            if (auto loaded = loadFields(script, m_registry.get<scripting::Scripts>(entity).entries.back().object); !loaded)
                return loaded;
        }
        return {};
    }

    world::Registry& m_registry;
    const reflect::TypeRegistry& m_types;
    const PrepareComponent& m_prepare;
    std::vector<world::Entity> m_created;
    std::vector<std::uint64_t> m_chain; // The scenes being placed, outermost first
};
}

auto registerComponents(reflect::TypeRegistry& types) -> std::expected<void, reflect::Error>
{
    auto builder = world::registerComponent<SceneInstance>(types, "SceneInstance");
    if (!builder)
        return std::unexpected{ builder.error() };
    // Read-only in the editor: a new scene needs the instance's entities recreated, not just the field written
    builder->displayName("Scene Instance").category("Scene").field("scene", &SceneInstance::scene, { .readOnly = true });
    return builder->status();
}

auto deriveEntityId(std::uint64_t holderId, std::uint64_t localId) -> std::uint64_t
{
    std::array<unsigned char, 16> bytes{};
    for (std::size_t i = 0; i < 8; ++i)
    {
        bytes[i] = static_cast<unsigned char>(holderId >> (8 * i));
        bytes[8 + i] = static_cast<unsigned char>(localId >> (8 * i));
    }
    return XXH3_64bits(bytes.data(), bytes.size());
}

auto parseScene(std::string_view text, const reflect::TypeRegistry& types) -> std::expected<Scene, Error>
{
    static constexpr auto keys = std::to_array<std::string_view>({ "type", "id", "version", "types", "entities" });

    auto root = parseJson(text);
    if (!root)
        return std::unexpected{ root.error() };
    auto header = readHeader(*root, sceneType, sceneVersion, keys);
    if (!header)
        return std::unexpected{ header.error() };

    auto typeTable = required(*root, "types", Node::Kind::Object, "the scene");
    if (!typeTable)
        return std::unexpected{ typeTable.error() };
    std::map<const reflect::TypeInfo*, std::uint32_t> versions;
    for (const Node& entry : (*typeTable)->items)
    {
        const reflect::TypeInfo* info = types.find(entry.key);
        if (!info)
            return makeError(ErrorCode::UnknownType, std::format("no type named '{}' is registered", entry.key), entry.line);
        auto typeVersion = parseVersion(entry, std::format("the version of '{}'", entry.key));
        if (!typeVersion)
            return std::unexpected{ typeVersion.error() };
        if (!versions.emplace(info, *typeVersion).second)
            return makeError(ErrorCode::InvalidData, std::format("'{}' is listed twice in \"types\"", info->name), entry.line);
    }

    auto entityList = required(*root, "entities", Node::Kind::Array, "the scene");
    if (!entityList)
        return std::unexpected{ entityList.error() };

    Scene scene{ .id = header->id };
    std::vector<const Node*> childLists; // Each entity's "children" node, for the lines of errors about them
    std::unordered_set<std::uint64_t> ids;
    for (const Node& node : (*entityList)->items)
    {
        auto entity = readEntity(node, types, versions);
        if (!entity)
            return std::unexpected{ entity.error() };
        if (!ids.insert(entity->id).second)
            return makeError(ErrorCode::DuplicateEntity, std::format("the entity {} is listed twice", formatId(entity->id)), entity->line);
        childLists.push_back(node.find("children"));
        scene.entities.push_back(std::move(*entity));
    }

    if (auto checked = checkHierarchy(scene.entities, childLists); !checked)
        return std::unexpected{ checked.error() };

    std::ranges::sort(scene.entities, {}, &SceneEntity::id);
    return scene;
}

auto writeScene(const Scene& scene) -> std::string
{
    std::map<std::string, std::uint32_t> typeVersions;
    for (const SceneEntity& entity : scene.entities)
    {
        for (const auto* list : { &entity.components, &entity.scripts })
        {
            for (const SceneComponent& component : *list)
                typeVersions.emplace(component.type->name, component.type->version);
        }
    }

    Node root = writeHeader(sceneType, scene.id, sceneVersion);
    Node& table = root.add("types", Node::makeObject());
    for (const auto& [name, version] : typeVersions)
        table.add(name, Node::makeNumber(std::to_string(version)));

    std::vector<const SceneEntity*> sorted;
    for (const SceneEntity& entity : scene.entities)
        sorted.push_back(&entity);
    std::ranges::sort(sorted, {}, [](const SceneEntity* entity) { return entity->id; });

    Node& entities = root.add("entities", Node::makeArray());
    for (const SceneEntity* entity : sorted)
    {
        Node& node = entities.push(Node::makeObject());
        node.add("id", Node::makeString(formatId(entity->id)));
        node.add("name", Node::makeString(entity->name));
        Node& children = node.add("children", Node::makeArray());
        for (std::uint64_t child : entity->children)
            children.push(Node::makeString(formatId(child)));

        std::vector<const SceneComponent*> components;
        for (const SceneComponent& component : entity->components)
            components.push_back(&component);
        std::ranges::sort(components, {}, [](const SceneComponent* component) -> const std::string& { return component->type->name; });
        Node& componentNode = node.add("components", Node::makeObject());
        for (const SceneComponent* component : components)
            componentNode.add(component->type->name, orderedFields(*component));

        if (!entity->scripts.empty())
        {
            Node& scripts = node.add("scripts", Node::makeArray());
            for (const SceneComponent& script : entity->scripts)
                scripts.push(Node::makeObject()).add(script.type->name, orderedFields(script));
        }
    }
    return writeJson(root);
}

auto saveScene(world::Registry& registry, const reflect::TypeRegistry& types, std::uint64_t id) -> std::expected<Scene, Error>
{
    const auto stored = storedComponents(types);
    Scene scene{ .id = id };
    for (const auto handle : registry.view<StableId>())
    {
        const world::Entity entity{ handle };
        if (registry.has<InstanceMember>(entity))
            continue;
        auto saved = saveEntity(registry, types, stored, entity);
        if (!saved)
            return std::unexpected{ std::move(saved.error()) };
        scene.entities.push_back(std::move(*saved));
    }
    std::ranges::sort(scene.entities, {}, &SceneEntity::id);
    return scene;
}

auto saveScene(world::Registry& registry, const reflect::TypeRegistry& types, world::Entity root, std::uint64_t id) -> std::expected<Scene, Error>
{
    if (!registry.valid(root))
        return makeError(ErrorCode::MissingEntity, "the root of the subtree is not an entity of the world");
    if (registry.has<InstanceMember>(root))
    {
        return makeError(ErrorCode::InvalidData,
            std::format("{} belongs to a scene instance; capture its holder instead", formatId(registry.get<StableId>(root).value)));
    }

    const auto stored = storedComponents(types);
    Scene scene{ .id = id };
    std::vector<world::Entity> pending{ root };
    while (!pending.empty())
    {
        const world::Entity entity = pending.back();
        pending.pop_back();
        auto saved = saveEntity(registry, types, stored, entity);
        if (!saved)
            return std::unexpected{ std::move(saved.error()) };
        scene.entities.push_back(std::move(*saved));

        // An instance's entities are left out, and so are theirs: all of them are instance members
        for (world::Entity child = registry.get<Children>(entity).first; child; child = registry.get<Siblings>(child).next)
        {
            if (!registry.has<InstanceMember>(child))
                pending.push_back(child);
        }
    }
    std::ranges::sort(scene.entities, {}, &SceneEntity::id);
    return scene;
}

auto instantiate(world::Registry& registry, const reflect::TypeRegistry& types, const Scene& scene, const PrepareComponent& prepare)
    -> std::expected<void, Error>
{
    Instantiator instantiator{ registry, types, prepare };
    if (auto placed = instantiator.place(scene, world::Entity{}); !placed)
    {
        instantiator.rollback();
        return std::unexpected{ std::move(placed.error()) };
    }
    return {};
}

auto instantiate(world::Registry& registry, const reflect::TypeRegistry& types, const Scene& subtree, world::Entity parent, std::size_t index,
    EntityIds ids, const PrepareComponent& prepare) -> std::expected<world::Entity, Error>
{
    if (parent && !registry.valid(parent))
        return makeError(ErrorCode::MissingEntity, "the parent of the subtree is not an entity of the world");
    if (parent && registry.has<InstanceMember>(parent))
    {
        return makeError(ErrorCode::InvalidData,
            std::format("{} belongs to a scene instance; it cannot get children of its own", formatId(registry.get<StableId>(parent).value)));
    }

    const std::optional<Scene> renamed = ids == EntityIds::New ? std::optional{ withNewIds(subtree, registry) } : std::nullopt;
    const Scene& placed = renamed ? *renamed : subtree;
    auto rootId = findRoot(placed);
    if (!rootId)
        return std::unexpected{ std::move(rootId.error()) };

    Instantiator instantiator{ registry, types, prepare };
    if (auto created = instantiator.place(placed, world::Entity{}); !created)
    {
        instantiator.rollback();
        return std::unexpected{ std::move(created.error()) };
    }

    const world::Entity root = registry.find(StableId{ *rootId });
    if (parent)
        std::ignore = registry.insertChild(parent, root, index);
    return root;
}

auto instantiate(world::Registry& registry, const reflect::TypeRegistry& types, const assets::AssetRef<Scene>& scene,
    world::Entity parent, const PrepareComponent& prepare) -> std::expected<world::Entity, Error>
{
    if (!scene)
        return makeError(ErrorCode::MissingScene, std::format("the scene {} is not loaded", formatId(scene.id().value)));

    const world::Entity holder = registry.create();
    registry.add<SceneInstance>(holder, SceneInstance{ scene });
    if (parent)
        std::ignore = registry.addChild(parent, holder);

    Instantiator instantiator{ registry, types, prepare };
    if (auto placed = instantiator.place(*scene, holder); !placed)
    {
        instantiator.rollback();
        registry.destroy(holder);
        return std::unexpected{ std::move(placed.error()) };
    }
    return holder;
}
}
