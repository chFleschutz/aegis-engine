module;
#include <cassert>
#include <cstdint>
#include <memory>
#include <optional>

// Only the ECS headers: entt.hpp pulls in entt::meta, which crashes clang -O3 against libstdc++
#include <entt/entity/entity.hpp>
#include <entt/entity/registry.hpp>
#include <entt/entity/view.hpp>

module aegis.render_graph;
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
    m_registry = &registry;

    for (auto&& [entity, material] : registry.view<assets::Material>().each())
    {
        if (material.instance)
            addInstance(material.instance->materialTemplate(), registry.has<aegis::DynamicTag>(aegis::scene::Entity{ entity }));
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

auto DrawBatchRegistry::addInstance(const std::shared_ptr<const assets::MaterialTemplate>& materialTemplate,
    bool dynamic) -> void
{
    assert(materialTemplate);
    auto [found, inserted] = m_batchIds.try_emplace(materialTemplate.get(), static_cast<std::uint32_t>(m_batches.size()));
    if (inserted)
    {
        assert(m_batches.size() < maxDrawBatches && "Too many draw batches");
        m_batches.push_back({ .id = found->second, .firstInstance = 0, .instanceCount = 0, .materialTemplate = materialTemplate });
    }

    ++m_batches[found->second].instanceCount;
    ++(dynamic ? m_dynamicCount : m_staticCount);
    updateOffsets();
}

auto DrawBatchRegistry::removeInstance(const assets::MaterialTemplate& materialTemplate, bool dynamic) -> void
{
    const auto id = batchId(materialTemplate);
    assert(id && m_batches[*id].instanceCount > 0 && "Removing an instance that was never added");

    --m_batches[*id].instanceCount;
    --(dynamic ? m_dynamicCount : m_staticCount);
    updateOffsets();
}

auto DrawBatchRegistry::setDynamic(bool dynamic) -> void
{
    if (dynamic)
    {
        --m_staticCount;
        ++m_dynamicCount;
    }
    else
    {
        ++m_staticCount;
        --m_dynamicCount;
    }
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

auto DrawBatchRegistry::onMaterialConstruct(entt::registry& registry, entt::entity entity) -> void
{
    const auto& material = registry.get<assets::Material>(entity);
    if (material.instance)
        addInstance(material.instance->materialTemplate(), registry.all_of<aegis::DynamicTag>(entity));
}

auto DrawBatchRegistry::onMaterialDestroy(entt::registry& registry, entt::entity entity) -> void
{
    const auto& material = registry.get<assets::Material>(entity);
    if (material.instance)
        removeInstance(*material.instance->materialTemplate(), registry.all_of<aegis::DynamicTag>(entity));
}

auto DrawBatchRegistry::onDynamicConstruct(entt::registry& registry, entt::entity entity) -> void
{
    if (registry.all_of<assets::Material>(entity))
        setDynamic(true);
}

auto DrawBatchRegistry::onDynamicDestroy(entt::registry& registry, entt::entity entity) -> void
{
    if (registry.all_of<assets::Material>(entity))
        setDynamic(false);
}
}
