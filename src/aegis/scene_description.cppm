module;

#include <concepts>

export module Aegis.SceneDescription;

import Aegis.Scene;
import Aegis.Scripting.ScriptManager;

export namespace Aegis
{
	/// @brief Base class for creating a scene
	class SceneDescription
	{
	public:
		virtual ~SceneDescription() = default;

		/// @brief All objects in a scene are created here
		virtual void initialize(Scene::Scene& scene, Scripting::ScriptManager& scripts) = 0;
	};

	template<typename T>
	concept SceneDescriptionDerived = std::derived_from<T, SceneDescription>;
}
