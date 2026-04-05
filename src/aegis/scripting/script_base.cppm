export module Aegis.Scripting.ScriptBase;

import Aegis.Scene.Entity;

export namespace Aegis::Scripting
{
	class ScriptBase
	{
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
		bool has() const
		{
			return m_entity.has<T...>();
		}

		/// @brief Returns a reference of the component of type T
		/// @note Component of type T must exist
		template<typename T>
		T& get() const
		{
			return m_entity.get<T>();
		}

		/// @brief Adds a component of type T to the entity and returns a reference to it
		template<typename T, typename... Args>
		T& add(Args&&... args)
		{
			return m_entity.add<T>(std::forward<Args>(args)...);
		}

		/// @brief Returns the entity
		Scene::Entity entity() const { return m_entity; }

	private:
		Scene::Entity m_entity;

		friend class Scene::Entity;
	};
}
