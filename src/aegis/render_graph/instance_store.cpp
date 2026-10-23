module;
#include <cassert>

module aegis.render_graph;
import std;
import aegis.assets;
import aegis.math;

namespace aegis::render_graph
{
namespace
{
auto markDirty(DirtyRange& dirty, std::uint32_t position) -> void
{
    dirty = dirty.merged({ position, position + 1 });
}
}

auto InstanceStore::add(Instance instance, const std::shared_ptr<const assets::MaterialTemplate>& materialTemplate,
    bool dynamic) -> std::expected<InstanceId, Error>
{
    assert(materialTemplate && "An instance needs a material template");
    auto batch = acquireBatch(materialTemplate);
    if (!batch)
        return std::unexpected{ batch.error() };

    InstanceId id{ static_cast<std::uint32_t>(m_locations.size()) };
    if (!m_freeIds.empty())
    {
        id.index = m_freeIds.back();
        m_freeIds.pop_back();
    }
    else
    {
        m_locations.emplace_back();
    }

    instance.drawBatch = *batch;
    ++m_batches[*batch].instanceCount;
    addMaterial(instance.material);
    insert(id, std::move(instance), dynamic);
    updateOffsets();
    return id;
}

auto InstanceStore::remove(InstanceId id) -> void
{
    const auto instance = extract(id);
    --m_batches[instance.drawBatch].instanceCount;
    removeMaterial(instance.material.get());
    m_locations[id.index].reset();
    m_freeIds.push_back(id.index);
    updateOffsets();
}

auto InstanceStore::update(InstanceId id, Instance instance,
    const std::shared_ptr<const assets::MaterialTemplate>& materialTemplate) -> std::expected<void, Error>
{
    assert(materialTemplate && "An instance needs a material template");
    const auto [dynamic, position] = location(id);
    auto& current = set(dynamic).instances[position];

    if (materialTemplate != m_batches[current.drawBatch].materialTemplate)
    {
        auto batch = acquireBatch(materialTemplate);
        if (!batch)
            return std::unexpected{ batch.error() };
        --m_batches[current.drawBatch].instanceCount;
        ++m_batches[*batch].instanceCount;
        current.drawBatch = *batch;
        updateOffsets();
        markDirty(set(dynamic).dirty, position);
    }
    if (instance.material != current.material)
    {
        addMaterial(instance.material);
        removeMaterial(current.material.get());
        current.material = std::move(instance.material);
        markDirty(set(dynamic).dirty, position);
    }
    if (instance.mesh != current.mesh)
    {
        current.mesh = std::move(instance.mesh);
        markDirty(set(dynamic).dirty, position);
    }
    if (!detail::sameModel(instance.model, current.model))
    {
        current.model = instance.model;
        markDirty(set(dynamic).dirty, position);
    }
    return {};
}

auto InstanceStore::setDynamic(InstanceId id, bool dynamic) -> void
{
    if (location(id).dynamic == dynamic)
        return;
    insert(id, extract(id), dynamic);
}

auto InstanceStore::clear() -> void
{
    m_static = {};
    m_dynamic = {};
    m_locations.clear();
    m_freeIds.clear();
    m_batches.clear();
    m_batchIds.clear();
    m_materials.clear();
    m_materialUses.clear();
}

auto InstanceStore::contains(InstanceId id) const noexcept -> bool
{
    return id.index < m_locations.size() && m_locations[id.index].has_value();
}

auto InstanceStore::get(InstanceId id) const -> const Instance&
{
    const auto [dynamic, position] = location(id);
    return (dynamic ? m_dynamic : m_static).instances[position];
}

auto InstanceStore::isDynamic(InstanceId id) const -> bool
{
    return location(id).dynamic;
}

auto InstanceStore::clearDirty() noexcept -> void
{
    m_static.dirty = {};
    m_dynamic.dirty = {};
}

auto InstanceStore::markAllDirty() noexcept -> void
{
    m_static.dirty = DirtyRange::all();
    m_dynamic.dirty = DirtyRange::all();
}

auto InstanceStore::batchId(const assets::MaterialTemplate& materialTemplate) const -> std::optional<std::uint32_t>
{
    const auto found = m_batchIds.find(&materialTemplate);
    return found != m_batchIds.end() ? std::optional{ found->second } : std::nullopt;
}

auto InstanceStore::location(InstanceId id) const -> const Location&
{
    assert(contains(id) && "Instance id names no instance");
    return *m_locations[id.index];
}

auto InstanceStore::acquireBatch(const std::shared_ptr<const assets::MaterialTemplate>& materialTemplate)
    -> std::expected<std::uint32_t, Error>
{
    if (const auto found = batchId(*materialTemplate))
        return *found;
    if (m_batches.size() >= maxDrawBatches)
        return makeError(ErrorCode::LimitExceeded, std::format("Too many material templates: at most {} draw batches", maxDrawBatches));

    const auto id = static_cast<std::uint32_t>(m_batches.size());
    m_batchIds.emplace(materialTemplate.get(), id);
    m_batches.push_back({ .id = id, .firstInstance = 0, .instanceCount = 0, .materialTemplate = materialTemplate });
    return id;
}

auto InstanceStore::addMaterial(const std::shared_ptr<assets::Material>& material) -> void
{
    if (!material)
        return;
    const auto [use, inserted] = m_materialUses.try_emplace(material.get(), MaterialUse{ static_cast<std::uint32_t>(m_materials.size()), 0 });
    if (inserted)
        m_materials.push_back(material);
    ++use->second.count;
}

auto InstanceStore::removeMaterial(const assets::Material* material) -> void
{
    if (!material)
        return;
    const auto use = m_materialUses.find(material);
    assert(use != m_materialUses.end());
    if (--use->second.count > 0)
        return;

    // The last material takes the removed one's place.
    const auto index = use->second.index;
    m_materialUses.erase(use);
    if (index + 1 != m_materials.size())
    {
        m_materials[index] = std::move(m_materials.back());
        m_materialUses.at(m_materials[index].get()).index = index;
    }
    m_materials.pop_back();
}

auto InstanceStore::insert(InstanceId id, Instance instance, bool dynamic) -> void
{
    auto& target = set(dynamic);
    const auto position = static_cast<std::uint32_t>(target.instances.size());
    target.instances.push_back(std::move(instance));
    target.ids.push_back(id);
    markDirty(target.dirty, position);
    m_locations[id.index] = Location{ .dynamic = dynamic, .position = position };
}

auto InstanceStore::extract(InstanceId id) -> Instance
{
    const auto [dynamic, position] = location(id);
    auto& source = set(dynamic);
    auto instance = std::move(source.instances[position]);

    const auto last = static_cast<std::uint32_t>(source.instances.size() - 1);
    if (position != last)
    {
        source.instances[position] = std::move(source.instances[last]);
        source.ids[position] = source.ids[last];
        m_locations[source.ids[position].index]->position = position;
        markDirty(source.dirty, position);
    }
    source.instances.pop_back();
    source.ids.pop_back();
    return instance;
}

auto InstanceStore::updateOffsets() -> void
{
    std::uint32_t offset{ 0 };
    for (auto& batch : m_batches)
    {
        batch.firstInstance = offset;
        offset += batch.instanceCount;
    }
}
}
