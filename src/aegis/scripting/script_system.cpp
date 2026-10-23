module aegis.scripting.script_system;

import std;
import aegis.platform;

namespace aegis::scripting
{
ScriptSystem::ScriptSystem(world::Registry& registry, const platform::InputState& input, platform::Input* cursor) :
    m_registry(registry),
    m_input(input),
    m_cursor(cursor)
{
    m_registry.addDestroyListener(this, [this](world::Entity entity) { destroyScripts(entity); });
}

ScriptSystem::~ScriptSystem()
{
    destroyAll(); // Whatever still runs ends properly
    m_registry.removeDestroyListener(this);
}

void ScriptSystem::run(world::Registry& registry, float deltaSeconds, bool update)
{
    // Scripts spawn and destroy entities, which a view over Scripts must not see happen
    m_entities.clear();
    for (const auto entity : registry.view<Scripts>())
        m_entities.push_back(world::Entity{ entity });

    for (const auto entity : m_entities)
    {
        // Index-based: a hook can destroy the entity or attach scripts to it (they run in this loop),
        // which moves or removes the entries. Scripts attached to other entities start next frame.
        for (std::size_t i = 0; registry.valid(entity) && registry.has<Scripts>(entity) && i < registry.get<Scripts>(entity).entries.size(); i++)
        {
            auto& entry = registry.get<Scripts>(entity).entries[i];
            if (entry.state == ScriptEntry::State::Destroyed)
                continue;

            const auto script = entry.script; // Alive for the call even if the hook destroys the entity
            ScriptContext context{ registry, entity, m_input, m_cursor };
            if (entry.state == ScriptEntry::State::Attached)
            {
                entry.state = ScriptEntry::State::Created;
                script->onCreate(context);
                if (!registry.valid(entity))
                    break;
            }
            if (update)
                script->onUpdate(context, deltaSeconds);
        }
    }
}

void ScriptSystem::destroyScripts(world::Entity entity)
{
    if (!m_registry.valid(entity) || !m_registry.has<Scripts>(entity))
        return;

    for (std::size_t i = 0; i < m_registry.get<Scripts>(entity).entries.size(); i++)
    {
        auto& entry = m_registry.get<Scripts>(entity).entries[i];
        const bool running = entry.state == ScriptEntry::State::Created;
        entry.state = ScriptEntry::State::Destroyed;
        if (!running)
            continue;

        const auto script = entry.script;
        ScriptContext context{ m_registry, entity, m_input, m_cursor };
        script->onDestroy(context);
    }
}

void ScriptSystem::destroyAll()
{
    std::vector<world::Entity> entities;
    for (const auto entity : m_registry.view<Scripts>())
        entities.push_back(world::Entity{ entity });
    for (const auto entity : entities)
        destroyScripts(entity);
}
}
