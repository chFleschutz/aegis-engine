export module aegis.scripting.script_system;

import std;
import aegis.platform;
import aegis.world.system;
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
class ScriptSystem : public world::System
{
public:
    /// @param registry The registry of the world this system runs in (it must outlive the system)
    /// @param input What scripts see as input; it is read every frame, so whoever owns it updates it in place
    /// @param cursor Lets scripts change the cursor mode, or null in a world without a window
    ScriptSystem(world::Registry& registry, const platform::InputState& input, platform::Input* cursor = nullptr);
    /// @brief For a world without a window: scripts see no input
    explicit ScriptSystem(world::Registry& registry) :
        ScriptSystem(registry, m_noInput)
    {
    }
    ScriptSystem(const ScriptSystem&) = delete;
    ScriptSystem& operator=(const ScriptSystem&) = delete;

    ~ScriptSystem() override;

    /// @brief Creates the scripts that exist when the scene begins
    void onBegin(world::Registry& registry) override { run(registry, 0.0f, false); }

    void onUpdate(world::Registry& registry, float deltaSeconds) override { run(registry, deltaSeconds, true); }

    void onReset(world::Registry&) override { destroyAll(); }

private:
    /// @brief Creates the scripts that need it and optionally updates them all
    void run(world::Registry& registry, float deltaSeconds, bool update);

    /// @brief Calls onDestroy on the entity's running scripts
    void destroyScripts(world::Entity entity);

    void destroyAll();

    world::Registry& m_registry;
    const platform::InputState& m_input;
    platform::Input* m_cursor;
    platform::InputState m_noInput;        // What the constructor without input points at
    std::vector<world::Entity> m_entities; // Scratch for run()
};
}
