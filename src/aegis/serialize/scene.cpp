module aegis.serialize;
import std;
import aegis.reflect;
import aegis.scripting.script;
import aegis.world.registry;
import aegis.world.types;

namespace aegis::serialize
{
namespace
{
using reflect::Node;

constexpr std::string_view sceneType{ "Scene" };

auto formatId(std::uint64_t id) -> std::string
{
    return std::format("{:016x}", id);
}

auto expectKind(const Node& node, Node::Kind kind, std::string_view what) -> std::expected<void, Error>
{
    if (node.kind != kind)
        return makeError(ErrorCode::InvalidData, std::format("{} must be {}, not {}", what, reflect::nodeKindName(kind), reflect::nodeKindName(node.kind)), node.line);
    return {};
}

// The member `key` of `object`, which must be there and of kind `kind`.
auto required(const Node& object, std::string_view key, Node::Kind kind, std::string_view what) -> std::expected<const Node*, Error>
{
    const Node* found = object.find(key);
    if (!found)
        return makeError(ErrorCode::InvalidData, std::format("{} has no \"{}\"", what, key), object.line);
    if (auto checked = expectKind(*found, kind, std::format("\"{}\"", key)); !checked)
        return std::unexpected{ checked.error() };
    return found;
}

// The member `key` of `object` if it is there (null if not); it must be of kind `kind`.
auto optional(const Node& object, std::string_view key, Node::Kind kind) -> std::expected<const Node*, Error>
{
    const Node* found = object.find(key);
    if (found)
    {
        if (auto checked = expectKind(*found, kind, std::format("\"{}\"", key)); !checked)
            return std::unexpected{ checked.error() };
    }
    return found;
}

auto checkKeys(const Node& object, std::span<const std::string_view> known, std::string_view what) -> std::expected<void, Error>
{
    for (const Node& member : object.items)
    {
        if (!std::ranges::contains(known, std::string_view{ member.key }))
            return makeError(ErrorCode::InvalidData, std::format("{} has an unknown key \"{}\"", what, member.key), member.line);
    }
    return {};
}

// An ID as files write it: exactly 16 lowercase hex digits, and not zero.
auto parseId(const Node& node, std::string_view what) -> std::expected<std::uint64_t, Error>
{
    if (auto checked = expectKind(node, Node::Kind::String, what); !checked)
        return std::unexpected{ checked.error() };
    const std::string& text = node.text;
    const bool hex = std::ranges::all_of(text, [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
    if (text.size() != 16 || !hex)
        return makeError(ErrorCode::InvalidData, std::format("{} '{}' is not 16 lowercase hex digits", what, text), node.line);
    std::uint64_t id = 0;
    std::from_chars(text.data(), text.data() + text.size(), id, 16);
    if (id == 0)
        return makeError(ErrorCode::InvalidData, std::format("{} must not be zero", what), node.line);
    return id;
}

auto parseVersion(const Node& node, std::string_view what) -> std::expected<std::uint32_t, Error>
{
    if (auto checked = expectKind(node, Node::Kind::Number, what); !checked)
        return std::unexpected{ checked.error() };
    std::uint32_t version = 0;
    auto result = std::from_chars(node.text.data(), node.text.data() + node.text.size(), version);
    if (result.ec != std::errc{} || result.ptr != node.text.data() + node.text.size() || version == 0)
        return makeError(ErrorCode::InvalidData, std::format("{} must be a whole number of at least 1, not {}", what, node.text), node.line);
    return version;
}

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
}

auto parseScene(std::string_view text, const reflect::TypeRegistry& types) -> std::expected<Scene, Error>
{
    static constexpr auto keys = std::to_array<std::string_view>({ "type", "id", "version", "types", "entities" });

    auto root = parseJson(text);
    if (!root)
        return std::unexpected{ root.error() };
    if (auto checked = expectKind(*root, Node::Kind::Object, "a scene file"); !checked)
        return std::unexpected{ checked.error() };

    auto type = required(*root, "type", Node::Kind::String, "the file");
    if (!type)
        return std::unexpected{ type.error() };
    if ((*type)->text != sceneType)
        return makeError(ErrorCode::WrongType, std::format("the file is a '{}', not a '{}'", (*type)->text, sceneType), (*type)->line);
    if (auto checked = checkKeys(*root, keys, "the scene"); !checked)
        return std::unexpected{ checked.error() };

    auto idNode = required(*root, "id", Node::Kind::String, "the scene");
    if (!idNode)
        return std::unexpected{ idNode.error() };
    auto id = parseId(**idNode, "the scene's \"id\"");
    if (!id)
        return std::unexpected{ id.error() };

    auto versionNode = required(*root, "version", Node::Kind::Number, "the scene");
    if (!versionNode)
        return std::unexpected{ versionNode.error() };
    auto version = parseVersion(**versionNode, "\"version\"");
    if (!version)
        return std::unexpected{ version.error() };
    if (*version > sceneVersion)
    {
        return makeError(ErrorCode::UnsupportedVersion, std::format("the scene is version {}, newer than this engine's {}", *version, sceneVersion),
            (*versionNode)->line);
    }

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

    Scene scene{ .id = *id };
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

    Node root = Node::makeObject();
    root.add("type", Node::makeString(std::string{ sceneType }));
    root.add("id", Node::makeString(formatId(scene.id)));
    root.add("version", Node::makeNumber(std::to_string(sceneVersion)));
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
    // The registered components that live on entities, by name
    std::vector<std::pair<const reflect::TypeInfo*, const world::ComponentStorage*>> stored;
    for (const reflect::TypeInfo* info : types.types())
    {
        if (info->category != reflect::TypeCategory::Component)
            continue;
        if (const auto* storage = info->attachment<world::ComponentStorage>())
            stored.emplace_back(info, storage);
    }
    std::ranges::sort(stored, {}, [](const auto& entry) -> const std::string& { return entry.first->name; });

    Scene scene{ .id = id };
    for (const auto handle : registry.view<StableId>())
    {
        const world::Entity entity{ handle };
        SceneEntity saved{ .id = registry.get<StableId>(entity).value, .name = registry.get<Name>(entity).name };
        for (world::Entity child = registry.get<Children>(entity).first; child; child = registry.get<Siblings>(child).next)
            saved.children.push_back(registry.get<StableId>(child).value);

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
        scene.entities.push_back(std::move(saved));
    }
    std::ranges::sort(scene.entities, {}, &SceneEntity::id);
    return scene;
}

auto instantiate(world::Registry& registry, const reflect::TypeRegistry& types, const Scene& scene) -> std::expected<void, Error>
{
    for (const SceneEntity& entity : scene.entities)
    {
        if (registry.find(StableId{ entity.id }))
            return makeError(ErrorCode::DuplicateEntity, std::format("the world already has an entity {}", formatId(entity.id)), entity.line);
    }

    std::unordered_map<std::uint64_t, world::Entity> created;
    auto fail = [&](Error error) -> std::expected<void, Error> {
        for (const auto& [id, entity] : created)
            registry.destroy(entity);
        return std::unexpected{ std::move(error) };
    };

    for (const SceneEntity& saved : scene.entities)
    {
        const world::Entity entity = registry.createWithId(saved.id, saved.name);
        if (!entity)
            return fail(Error{ .code = ErrorCode::DuplicateEntity, .message = std::format("the entity {} is listed twice", formatId(saved.id)), .line = saved.line });
        created.emplace(saved.id, entity);

        for (const SceneComponent& component : saved.components)
        {
            const auto* storage = component.type->attachment<world::ComponentStorage>();
            if (!storage)
            {
                return fail(Error{ .code = ErrorCode::UnknownType,
                    .message = std::format("'{}' was not registered with world::registerComponent", component.type->name),
                    .line = component.fields.line });
            }
            if (auto loaded = loadFields(component, storage->emplace(registry, entity)); !loaded)
                return fail(loaded.error());
        }

        for (const SceneComponent& script : saved.scripts)
        {
            auto attached = scripting::attachScript(registry, types, entity, script.type->name);
            if (!attached)
            {
                return fail(Error{ .code = ErrorCode::UnknownType, .message = std::format("cannot attach the script '{}'", script.type->name), .line = script.fields.line, .reflect = attached.error() });
            }
            if (auto loaded = loadFields(script, registry.get<scripting::Scripts>(entity).entries.back().object); !loaded)
                return fail(loaded.error());
        }
    }

    for (const SceneEntity& saved : scene.entities)
    {
        const world::Entity parent = created.at(saved.id);
        for (std::uint64_t childId : saved.children)
        {
            auto child = created.find(childId);
            if (child == created.end())
                return fail(Error{ .code = ErrorCode::MissingEntity, .message = std::format("the child {} is not in the scene", formatId(childId)), .line = saved.line });
            if (registry.get<Parent>(child->second).entity)
                return fail(Error{ .code = ErrorCode::DuplicateEntity, .message = std::format("{} is listed as a child twice", formatId(childId)), .line = saved.line });
            if (!registry.addChild(parent, child->second))
                return fail(Error{ .code = ErrorCode::Cycle, .message = std::format("{} is its own ancestor", formatId(saved.id)), .line = saved.line });
        }
    }
    return {};
}
}
