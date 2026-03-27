module;

#include <vector>

export module Aegis.Scripting.ScriptManager;

import Aegis.Scripting.ScriptBase;

export namespace Aegis::Scripting
{
	class ScriptManager
	{
	public:
		ScriptManager() = default;
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
		void addScript(ScriptBase* script)
		{
			m_newScripts.emplace_back(script);
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

		std::vector<ScriptBase*> m_scripts;
		std::vector<ScriptBase*> m_newScripts;
	};
}
