module;

#include <cassert>

module aegis.scripting;

import std;
import aegis.assets;
import aegis.core;
import aegis.reflect;
import aegis.world;

namespace aegis::scripting
{
auto ScriptContext::spawn(assets::AssetId scene, world::Entity parent, const Transform& transform) const -> StableId
{
    auto* owner = m_registry.context<world::World>();
    if (!owner || !m_registry.context<Spawner>())
    {
        log::warn("Cannot spawn scene {}: the world has no spawner", scene);
        return StableId{};
    }
    if (parent && !m_registry.valid(parent))
    {
        log::warn("Cannot spawn scene {}: its parent is not alive", scene);
        return StableId{};
    }

    const std::uint64_t holderId = m_registry.unusedId();
    owner->defer([owner, scene, parent, transform, holderId](world::Registry& registry) {
        if (parent && !registry.valid(parent))
        {
            log::warn("Spawning scene {} skipped: its parent was destroyed", scene);
            return;
        }
        auto* spawner = registry.context<Spawner>();
        if (!spawner)
        {
            log::warn("Spawning scene {} skipped: the world has no spawner", scene);
            return;
        }
        auto holder = spawner->spawn(*owner, scene, parent, holderId);
        if (!holder)
        {
            log::warn("Spawning scene {} failed: {}", scene, holder.error());
            return;
        }
        registry.get<Transform>(*holder) = transform;
        registry.markStaticDirty(*holder);
    });
    return StableId{ holderId };
}

auto ScriptContext::destroy(world::Entity entity) const -> void
{
    auto* owner = m_registry.context<world::World>();
    if (!owner)
    {
        log::warn("Cannot destroy an entity: the registry belongs to no world");
        return;
    }
    owner->defer([entity](world::Registry& registry) {
        if (registry.valid(entity))
            registry.destroySubtree(entity);
    });
}

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
