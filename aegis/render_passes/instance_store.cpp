module;
#include <cassert>

module aegis.render_passes;
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

auto InstanceStore::add(Instance instance, const assets::MaterialVariant& variant, bool dynamic)
    -> std::expected<InstanceId, Error>
{
    assert(variant.materialTemplate && "An instance needs a material template");
    auto batch = acquireBatch(variant);
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

    instance.previousModel = instance.model;
    joinBatch(id, instance, *batch);
    addMaterial(instance.material, variant);
    insert(id, std::move(instance), dynamic);
    updateOffsets();
    return id;
}

auto InstanceStore::remove(InstanceId id) -> void
{
    const auto instance = extract(id);
    leaveBatch(instance);
    removeMaterial(instance.material.get());
    m_locations[id.index].reset();
    m_freeIds.push_back(id.index);
    updateOffsets();
}

auto InstanceStore::update(InstanceId id, Instance instance, const assets::MaterialVariant& variant)
    -> std::expected<void, Error>
{
    assert(variant.materialTemplate && "An instance needs a material template");
    const auto [dynamic, position] = location(id);
    auto& current = set(dynamic).instances[position];

    if (variant != m_batches[current.drawBatch].variant)
    {
        // Leaving first frees the current batch if this is its last instance, making room for the new variant at the
        // batch limit. Acquiring fails only if it was not freed, and then the instance rejoins it.
        // Rejoining takes the batch's last slot, so the instance's slot may change even then.
        const auto previous = current.drawBatch;
        leaveBatch(current);
        auto batch = acquireBatch(variant);
        joinBatch(id, current, batch ? *batch : previous);
        markDirty(set(dynamic).dirty, position);
        if (!batch)
            return std::unexpected{ batch.error() };
        updateOffsets();
    }
    if (instance.material != current.material)
    {
        addMaterial(instance.material, variant);
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
        // previousModel stays the model of the frame's start until endFrame.
        current.model = instance.model;
        markDirty(set(dynamic).dirty, position);
        m_moved.push_back(id);
    }
    return {};
}

auto InstanceStore::syncVariants() -> std::expected<void, Error>
{
    std::expected<void, Error> result;
    for (const auto& material : m_materials)
    {
        auto& use = m_materialUses.at(material.get());
        if (material->materialTemplate() == use.variant.materialTemplate && material->alphaMode() == use.variant.alphaMode &&
            material->cullMode() == use.variant.cullMode)
            continue;

        auto variant = material->variant();
        auto batch = acquireBatch(variant);
        if (!batch)
        {
            if (result)
                result = std::unexpected{ batch.error() };
            continue;
        }
        moveMaterial(material.get(), *batch);
        use.variant = std::move(variant);
    }
    return result;
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
    m_moved.clear();
    m_batches.clear();
    m_batchMembers.clear();
    m_freeBatchIds.clear();
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

auto InstanceStore::endFrame() noexcept -> void
{
    m_static.dirty = {};
    m_dynamic.dirty = {};
    for (const auto id : m_moved)
    {
        if (!contains(id))
            continue;
        const auto [dynamic, position] = location(id);
        auto& target = set(dynamic);
        auto& instance = target.instances[position];
        if (detail::sameModel(instance.previousModel, instance.model))
            continue;
        instance.previousModel = instance.model;
        markDirty(target.dirty, position);
    }
    m_moved.clear();
}

auto InstanceStore::markAllDirty() noexcept -> void
{
    m_static.dirty = DirtyRange::all();
    m_dynamic.dirty = DirtyRange::all();
    for (auto* target : { &m_static, &m_dynamic })
    {
        for (auto& instance : target->instances)
            instance.previousModel = instance.model;
    }
    m_moved.clear();
}

auto InstanceStore::batchId(const assets::MaterialVariant& variant) const -> std::optional<std::uint32_t>
{
    const auto found = m_batchIds.find(variant);
    return found != m_batchIds.end() ? std::optional{ found->second } : std::nullopt;
}

auto InstanceStore::location(InstanceId id) const -> const Location&
{
    assert(contains(id) && "Instance id names no instance");
    return *m_locations[id.index];
}

auto InstanceStore::acquireBatch(const assets::MaterialVariant& variant) -> std::expected<std::uint32_t, Error>
{
    if (const auto found = batchId(variant))
        return *found;

    std::uint32_t id{ 0 };
    if (!m_freeBatchIds.empty())
    {
        id = m_freeBatchIds.back();
        m_freeBatchIds.pop_back();
    }
    else if (m_batches.size() < maxDrawBatches)
    {
        id = static_cast<std::uint32_t>(m_batches.size());
        m_batches.push_back({ .id = id, .firstInstance = 0, .instanceCount = 0, .variant = {} });
        m_batchMembers.emplace_back();
    }
    else
    {
        return makeError(ErrorCode::LimitExceeded, std::format("Too many material variants: at most {} draw batches", maxDrawBatches));
    }
    m_batchIds.emplace(variant, id);
    m_batches[id].variant = variant;
    return id;
}

auto InstanceStore::joinBatch(InstanceId id, Instance& instance, std::uint32_t batch) -> void
{
    auto& members = m_batchMembers[batch];
    instance.drawBatch = batch;
    instance.batchSlot = static_cast<std::uint32_t>(members.size());
    members.push_back(id);
    ++m_batches[batch].instanceCount;
}

auto InstanceStore::leaveBatch(const Instance& instance) -> void
{
    auto& entry = m_batches[instance.drawBatch];
    auto& members = m_batchMembers[instance.drawBatch];
    assert(entry.instanceCount > 0 && "The batch has no instance to leave");
    assert(members.size() == entry.instanceCount && instance.batchSlot < members.size());

    // The batch's last instance takes the slot, which the GPU copy of it must learn.
    const auto slot = instance.batchSlot;
    if (slot + 1 != members.size())
    {
        members[slot] = members.back();
        const auto [dynamic, position] = location(members[slot]);
        auto& moved = set(dynamic);
        moved.instances[position].batchSlot = slot;
        markDirty(moved.dirty, position);
    }
    members.pop_back();

    if (--entry.instanceCount > 0)
        return;
    m_batchIds.erase(entry.variant);
    entry.variant = {};
    m_freeBatchIds.push_back(instance.drawBatch);
}

auto InstanceStore::addMaterial(const std::shared_ptr<assets::Material>& material, const assets::MaterialVariant& variant)
    -> void
{
    if (!material)
        return;
    const auto [use, inserted] = m_materialUses.try_emplace(material.get(),
        MaterialUse{ .index = static_cast<std::uint32_t>(m_materials.size()), .count = 0, .variant = variant });
    if (inserted)
        m_materials.push_back(material);
    ++use->second.count;
}

auto InstanceStore::moveMaterial(const assets::Material* material, std::uint32_t batch) -> void
{
    for (const bool dynamic : { false, true })
    {
        auto& source = set(dynamic);
        for (std::uint32_t position = 0; position < source.instances.size(); ++position)
        {
            auto& instance = source.instances[position];
            if (instance.material.get() != material || instance.drawBatch == batch)
                continue;
            leaveBatch(instance);
            joinBatch(source.ids[position], instance, batch);
            markDirty(source.dirty, position);
        }
    }
    updateOffsets();
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
