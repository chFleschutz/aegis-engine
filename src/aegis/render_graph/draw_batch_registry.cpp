module;
#include <cassert>

// Only the ECS headers: entt.hpp pulls in entt::meta, which crashes clang -O3 against libstdc++
#include <entt/entity/entity.hpp>
#include <entt/entity/registry.hpp>
#include <entt/entity/view.hpp>

module aegis.render_graph;
import std;
import aegis.assets;
import aegis.scene.registry;

namespace aegis::render_graph
{
DrawBatchRegistry::~DrawBatchRegistry()
{
    disconnect();
}

auto DrawBatchRegistry::connect(aegis::scene::Registry& registry) -> void
{
    disconnect();
    clear();
    m_registry = &registry;

    for (auto&& [entity, material] : registry.view<assets::Material>().each())
    {
        trackMaterial(entity, material, registry.has<aegis::DynamicTag>(aegis::scene::Entity{ entity }));
    }

    registry.onConstruct<assets::Material>().connect<&DrawBatchRegistry::onMaterialConstruct>(this);
    registry.onDestroy<assets::Material>().connect<&DrawBatchRegistry::onMaterialDestroy>(this);
    registry.onConstruct<aegis::DynamicTag>().connect<&DrawBatchRegistry::onDynamicConstruct>(this);
    registry.onDestroy<aegis::DynamicTag>().connect<&DrawBatchRegistry::onDynamicDestroy>(this);
}

auto DrawBatchRegistry::disconnect() -> void
{
    if (!m_registry)
        return;

    m_registry->onConstruct<assets::Material>().disconnect(this);
    m_registry->onDestroy<assets::Material>().disconnect(this);
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
            return makeError("Too many material templates: at most " + std::to_string(maxDrawBatches) + " draw batches");

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

auto DrawBatchRegistry::trackMaterial(entt::entity entity, const assets::Material& material, bool dynamic) -> void
{
    if (!material.instance)
        return;

    if (const auto tracked = track(entity, material.instance->materialTemplate(), dynamic); !tracked)
        std::println("DrawBatchRegistry: {}", tracked.error().message);
}

auto DrawBatchRegistry::onMaterialConstruct(entt::registry& registry, entt::entity entity) -> void
{
    trackMaterial(entity, registry.get<assets::Material>(entity), registry.all_of<aegis::DynamicTag>(entity));
}

auto DrawBatchRegistry::onMaterialDestroy(entt::registry&, entt::entity entity) -> void
{
    untrack(entity);
}

auto DrawBatchRegistry::onDynamicConstruct(entt::registry&, entt::entity entity) -> void
{
    setDynamic(entity, true);
}

auto DrawBatchRegistry::onDynamicDestroy(entt::registry&, entt::entity entity) -> void
{
    setDynamic(entity, false);
}
}
