module;

// Keeps std::derived_from and std::constructible_from visible to this interface (see AGENTS.md, import std)
#include <concepts>

export module aegis.scripting.script_manager;

import std;
import aegis.scene;
import aegis.scripting.script_base;
import aegis.platform;

export namespace aegis::scripting
{
class ScriptManager
{
public:
    ScriptManager(scene::Scene& scene, aegis::platform::Input& input) :
        m_scene(scene),
        m_input(input)
    {
    }
    ScriptManager(const ScriptManager&) = delete;
    ScriptManager(ScriptManager&&) = delete;
    ~ScriptManager()
    {
        handleNewScripts(); // just in case

        for (auto& script : m_scripts)
        {
            script->end();
        }
    }

    ScriptManager& operator=(const ScriptManager&) = delete;
    ScriptManager& operator=(ScriptManager&&) = delete;

    /// @brief Adds a script to call its virtual functions
    /// @param script The script to add
    template<typename T, typename... Args>
        requires std::derived_from<T, ScriptBase> && std::constructible_from<T, Args...>
    void addScript(scene::Entity entity, Args&&... args)
    {
        T* script = new T(std::forward<Args>(args)...);
        m_newScripts.emplace_back(script);
        script->init(&m_scene.registry(), entity, m_input);
    }

    /// @brief Calls the update function of each script
    void update(float deltaSeconds)
    {
        handleNewScripts();

        for (auto& script : m_scripts)
        {
            script->update(deltaSeconds);
        }
    }

private:
    /// @brief Calls the begin function of each script once
    void handleNewScripts()
    {
        for (auto& script : m_newScripts)
        {
            script->begin();
            m_scripts.emplace_back(script);
        }
        m_newScripts.clear();
    }

    scene::Scene& m_scene;
    aegis::platform::Input& m_input;
    std::vector<ScriptBase*> m_scripts;
    std::vector<ScriptBase*> m_newScripts;
};
}
