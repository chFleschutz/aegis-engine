module;

#include <cassert>

module aegis.editor.core;
import std;

namespace aegis::editor
{
void EditorContext::setWorld(world::World& world)
{
    assert(!playing() && "EditorContext::setWorld: stop playing first (exitPlay)");
    if (m_world != &world)
    {
        m_selection.clear();
        m_undo.clear();
    }
    m_world = &world;
}

void EditorContext::enterPlay(world::World& play)
{
    assert(!playing() && "EditorContext::enterPlay: already playing");
    m_editWorld = m_world;
    m_world = &play;
    m_editUndo = std::exchange(m_undo, UndoStack{});
}

void EditorContext::exitPlay()
{
    assert(playing() && "EditorContext::exitPlay: not playing");
    m_world = std::exchange(m_editWorld, nullptr);
    m_undo = std::exchange(m_editUndo, UndoStack{});
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
