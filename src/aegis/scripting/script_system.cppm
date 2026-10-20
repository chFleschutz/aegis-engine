export module aegis.scripting.script_system;

import std;
import aegis.platform;
import aegis.scene.system;
export import aegis.scripting.script;

export namespace aegis::scripting
{
/// @brief Runs the scripts of every entity that has a Scripts component
///
/// Per entity, in attach order: onCreate once (before the script's first onUpdate), then onUpdate every
/// frame. onDestroy runs when the entity is destroyed and when the scene is reset, while the entity
/// still has all its components. A script that was never created gets no onDestroy.
/// Add it before the systems that read what scripts write (the TransformSystem), so a script's change
/// shows up in the same frame.
class ScriptSystem : public scene::System
{
public:
    /// @param registry The registry of the world this system runs in (it must outlive the system)
    /// @param input The input scripts see, or null in a world without a window
    explicit ScriptSystem(scene::Registry& registry, platform::Input* input = nullptr) :
        m_registry(registry),
        m_input(input)
    {
        m_registry.addDestroyListener(this, [this](scene::Entity entity) { destroyScripts(entity); });
    }
    ScriptSystem(const ScriptSystem&) = delete;
    ScriptSystem& operator=(const ScriptSystem&) = delete;

    ~ScriptSystem() override
    {
        destroyAll(); // Whatever still runs ends properly
        m_registry.removeDestroyListener(this);
    }

    /// @brief Creates the scripts that exist when the scene begins
    void onBegin(scene::Registry& registry) override { run(registry, 0.0f, false); }

    void onUpdate(scene::Registry& registry, float deltaSeconds) override { run(registry, deltaSeconds, true); }

    void onReset(scene::Registry&) override { destroyAll(); }

private:
    /// @brief Creates the scripts that need it and optionally updates them all
    void run(scene::Registry& registry, float deltaSeconds, bool update)
    {
        // Scripts spawn and destroy entities, which a view over Scripts must not see happen
        m_entities.clear();
        for (const auto entity : registry.view<Scripts>())
            m_entities.push_back(scene::Entity{ entity });

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
                ScriptContext context{ registry, entity, m_input };
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

    /// @brief Calls onDestroy on the entity's running scripts
    void destroyScripts(scene::Entity entity)
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
            ScriptContext context{ m_registry, entity, m_input };
            script->onDestroy(context);
        }
    }

    void destroyAll()
    {
        std::vector<scene::Entity> entities;
        for (const auto entity : m_registry.view<Scripts>())
            entities.push_back(scene::Entity{ entity });
        for (const auto entity : entities)
            destroyScripts(entity);
    }

    scene::Registry& m_registry;
    platform::Input* m_input;
    std::vector<scene::Entity> m_entities; // Scratch for run()
};
}
