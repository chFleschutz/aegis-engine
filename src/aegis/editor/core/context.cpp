module aegis.editor.core;
import std;

namespace aegis::editor
{
void EditorContext::setWorld(world::World& world)
{
    if (m_world != &world)
        m_selection.clear();
    m_world = &world;
}

auto EditorContext::selectedEntity() const -> world::Entity
{
    const auto active = m_selection.active();
    if (!m_world || !active)
        return {};
    return m_world->registry().find(*active);
}

auto EditorContext::isSelected(world::Entity entity) const -> bool
{
    const auto id = idOf(entity);
    return id && m_selection.contains(*id);
}

void EditorContext::select(world::Entity entity)
{
    if (const auto id = idOf(entity))
        m_selection.select(*id);
    else
        m_selection.clear();
}

auto EditorContext::idOf(world::Entity entity) const -> std::optional<StableId>
{
    if (!m_world)
        return std::nullopt;
    auto& registry = m_world->registry();
    if (!registry.valid(entity) || !registry.has<StableId>(entity))
        return std::nullopt;
    return registry.get<StableId>(entity);
}
}
