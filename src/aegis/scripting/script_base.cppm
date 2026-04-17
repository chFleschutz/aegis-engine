export module Aegis.Scripting.ScriptBase;

import Aegis.Scene.Entity;
import Aegis.Scene.Registry;

export namespace Aegis::Scripting
{
	class ScriptBase
	{
		friend class ScriptManager;

	public:
		/// @brief Constructor
		/// @note When overriding, dont't use member functions (m_entity is not initialized yet)
		ScriptBase() = default;
		virtual ~ScriptBase() = default;

		/// @brief Called once at the first frame just before update
		virtual void begin() {}
		/// @brief Called every frame with the delta of the last two frames in seconds
		virtual void update(float deltaSeconds) {}
		/// @brief Called once at the end of the last frame
		virtual void end() {}

		/// @brief Returns true if the entity has all components of type T...
		template<typename... T>
		auto has() const -> bool
		{
			return m_registry->has<T...>(m_entity);
		}

		/// @brief Returns a reference of the component of type T
		/// @note Component of type T must exist
		template<typename T>
		auto get() const -> T&
		{
			return m_registry->get<T>(m_entity);
		}

		/// @brief Adds a component of type T to the entity and returns a reference to it
		//template<typename Component, typename... Args>
		//auto add(Args&&... args) -> Component&
		//{
		//	return m_registry->add<Component>(m_entity, std::forward<Args>(args)...);
		//}

		/// @brief Returns the entity
		auto entity() const -> Scene::Entity { return m_entity; }

	private:
		Scene::Registry* m_registry{ nullptr };
		Scene::Entity m_entity;
	};
}
