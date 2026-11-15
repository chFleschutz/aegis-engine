module aegis.scripting.script_system;

import std;
import aegis.platform;

namespace aegis::scripting
{
void ScriptSystem::onAttach(world::Registry& registry)
{
    registry.addDestroyListener(this, [this](world::Registry& owner, world::Entity entity) { destroyScripts(owner, entity); });
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

void ScriptSystem::destroyScripts(world::Registry& registry, world::Entity entity)
{
    if (!registry.valid(entity) || !registry.has<Scripts>(entity))
        return;

    for (std::size_t i = 0; i < registry.get<Scripts>(entity).entries.size(); i++)
    {
        auto& entry = registry.get<Scripts>(entity).entries[i];
        const bool running = entry.state == ScriptEntry::State::Created;
        entry.state = ScriptEntry::State::Destroyed;
        if (!running)
            continue;

        const auto script = entry.script;
        ScriptContext context{ registry, entity, m_input, m_cursor };
        script->onDestroy(context);
    }
}

void ScriptSystem::destroyAll(world::Registry& registry)
{
    std::vector<world::Entity> entities;
    for (const auto entity : registry.view<Scripts>())
        entities.push_back(world::Entity{ entity });
    for (const auto entity : entities)
        destroyScripts(registry, entity);
}
}
