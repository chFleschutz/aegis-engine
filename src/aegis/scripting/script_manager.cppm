module;

#include <vector>

export module Aegis.Scripting.ScriptManager;

import Aegis.Scene;
import Aegis.Scripting.ScriptBase;

export namespace Aegis::Scripting
{
	class ScriptManager
	{
	public:
		ScriptManager(Scene::Scene& scene) : m_scene(scene) {}
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
		template <typename T, typename... Args>
			requires std::derived_from<T, ScriptBase>&& std::constructible_from<T, Args...>
		void addScript(Scene::Entity entity, Args&&... args)
		{
			T* script = new T(std::forward<Args>(args)...);
			m_newScripts.emplace_back(script);
			script->init(&m_scene.registry(), entity);
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

		Scene::Scene& m_scene;
		std::vector<ScriptBase*> m_scripts;
		std::vector<ScriptBase*> m_newScripts;
	};
}
