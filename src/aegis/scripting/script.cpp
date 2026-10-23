module;

#include <cassert>

module aegis.scripting.script;

import std;
import aegis.reflect;
import aegis.world.registry;

namespace aegis::scripting
{
auto attachScript(world::Registry& registry, const reflect::TypeRegistry& types, world::Entity entity, std::string_view name)
    -> std::expected<Script*, reflect::Error>
{
    assert(registry.valid(entity) && "Cannot attach a script: Entity is not valid");

    const reflect::TypeInfo* info = types.find(name);
    if (!info)
        return reflect::makeError(reflect::ErrorCode::UnknownType, std::format("no script named '{}' is registered", name));
    if (info->category != reflect::TypeCategory::Script)
        return reflect::makeError(reflect::ErrorCode::WrongCategory, std::format("'{}' is a component, not a script", name));

    auto created = info->create();
    if (!created)
        return std::unexpected{ created.error() };

    Script* base = info->as<Script>(created->get());
    if (!base)
        return reflect::makeError(reflect::ErrorCode::WrongCategory, std::format("'{}' was not registered with registerScript", name));
    auto script = std::shared_ptr<Script>{ *created, base };
    Script* result = script.get();
    registry.getOrAdd<Scripts>(entity).entries.push_back({ .name = info->name, .script = std::move(script), .object = created->get() });
    return result;
}
}
