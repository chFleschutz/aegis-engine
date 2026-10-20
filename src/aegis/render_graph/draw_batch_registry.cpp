module;
#include <cassert>

// Only the ECS headers: entt.hpp pulls in entt::meta, which crashes clang -O3 against libstdc++
#include <entt/entity/entity.hpp>
#include <entt/entity/registry.hpp>
#include <entt/entity/view.hpp>

module aegis.render_graph;
import std;
import aegis.assets;
import aegis.world.render_components;
import aegis.base;
import aegis.world.registry;

namespace aegis::render_graph
{
DrawBatchRegistry::~DrawBatchRegistry()
{
    disconnect();
}

auto DrawBatchRegistry::connect(aegis::world::Registry& registry) -> void
{
    disconnect();
    clear();
    m_registry = &registry;

    for (auto&& [entity, renderer] : registry.view<aegis::MeshRenderer>().each())
    {
        trackRenderer(entity, renderer, registry.has<aegis::DynamicTag>(aegis::world::Entity{ entity }));
    }

    registry.onConstruct<aegis::MeshRenderer>().connect<&DrawBatchRegistry::onRendererConstruct>(this);
    registry.onDestroy<aegis::MeshRenderer>().connect<&DrawBatchRegistry::onRendererDestroy>(this);
    registry.onConstruct<aegis::DynamicTag>().connect<&DrawBatchRegistry::onDynamicConstruct>(this);
    registry.onDestroy<aegis::DynamicTag>().connect<&DrawBatchRegistry::onDynamicDestroy>(this);
    registry.invalidateStatic();
}

auto DrawBatchRegistry::disconnect() -> void
{
    if (!m_registry)
        return;

    m_registry->onConstruct<aegis::MeshRenderer>().disconnect(this);
    m_registry->onDestroy<aegis::MeshRenderer>().disconnect(this);
    m_registry->onConstruct<aegis::DynamicTag>().disconnect(this);
    m_registry->onDestroy<aegis::DynamicTag>().disconnect(this);
    m_registry = nullptr;
}

auto DrawBatchRegistry::batchId(const assets::MaterialTemplate& materialTemplate) const -> std::optional<std::uint32_t>
{
    const auto found = m_batchIds.find(&materialTemplate);
    return found != m_batchIds.end() ? std::optional{ found->second } : std::nullopt;
}

auto DrawBatchRegistry::track(entt::entity entity, const std::shared_ptr<const assets::MaterialTemplate>& materialTemplate,
    bool dynamic) -> std::expected<void, Error>
{
    assert(materialTemplate);
    untrack(entity);

    auto id = static_cast<std::uint32_t>(m_batches.size());
    const auto found = m_batchIds.find(materialTemplate.get());
    if (found != m_batchIds.end())
    {
        id = found->second;
    }
    else
    {
        if (m_batches.size() >= maxDrawBatches)
            return makeError(ErrorCode::LimitExceeded, "Too many material templates: at most " + std::to_string(maxDrawBatches) + " draw batches");

        m_batchIds.emplace(materialTemplate.get(), id);
        m_batches.push_back({ .id = id, .firstInstance = 0, .instanceCount = 0, .materialTemplate = materialTemplate });
    }

    ++m_batches[id].instanceCount;
    ++(dynamic ? m_dynamicCount : m_staticCount);
    m_tracked.emplace(entity, Tracked{ .materialTemplate = materialTemplate.get(), .dynamic = dynamic });
    updateOffsets();
    return {};
}

auto DrawBatchRegistry::untrack(entt::entity entity) -> void
{
    const auto found = m_tracked.find(entity);
    if (found == m_tracked.end())
        return;

    const auto [materialTemplate, dynamic] = found->second;
    m_tracked.erase(found);
    --m_batches[m_batchIds.at(materialTemplate)].instanceCount;
    --(dynamic ? m_dynamicCount : m_staticCount);
    updateOffsets();
}

auto DrawBatchRegistry::setDynamic(entt::entity entity, bool dynamic) -> void
{
    const auto found = m_tracked.find(entity);
    if (found == m_tracked.end() || found->second.dynamic == dynamic)
        return;

    found->second.dynamic = dynamic;
    --(dynamic ? m_staticCount : m_dynamicCount);
    ++(dynamic ? m_dynamicCount : m_staticCount);
}

auto DrawBatchRegistry::clear() -> void
{
    m_batches.clear();
    m_batchIds.clear();
    m_tracked.clear();
    m_staticCount = 0;
    m_dynamicCount = 0;
}

auto DrawBatchRegistry::updateOffsets() -> void
{
    std::uint32_t offset{ 0 };
    for (auto& batch : m_batches)
    {
        batch.firstInstance = offset;
        offset += batch.instanceCount;
    }
}

auto DrawBatchRegistry::trackRenderer(entt::entity entity, const aegis::MeshRenderer& renderer, bool dynamic) -> void
{
    if (!renderer.material)
        return;

    if (const auto tracked = track(entity, renderer.material->materialTemplate(), dynamic); !tracked)
        log::warn("DrawBatchRegistry: {} not drawn on the GPU-driven path: {}", static_cast<std::uint32_t>(entity), describe(tracked.error()));
}

auto DrawBatchRegistry::markStatic(entt::registry& registry, entt::entity entity) -> void
{
    if (registry.all_of<aegis::DynamicTag>(entity))
        m_registry->invalidateStatic();
    else
        m_registry->markStaticDirty(aegis::world::Entity{ entity });
}

auto DrawBatchRegistry::onRendererConstruct(entt::registry& registry, entt::entity entity) -> void
{
    trackRenderer(entity, registry.get<aegis::MeshRenderer>(entity), registry.all_of<aegis::DynamicTag>(entity));
    markStatic(registry, entity);
}

auto DrawBatchRegistry::onRendererDestroy(entt::registry&, entt::entity entity) -> void
{
    untrack(entity);
    m_registry->invalidateStatic();
}

auto DrawBatchRegistry::onDynamicConstruct(entt::registry&, entt::entity entity) -> void
{
    setDynamic(entity, true);
    m_registry->invalidateStatic();
}

auto DrawBatchRegistry::onDynamicDestroy(entt::registry&, entt::entity entity) -> void
{
    setDynamic(entity, false);
    m_registry->invalidateStatic();
}
}
